#include "epub.h"
#include "unzip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ---- Internal helpers ----

// Create all directories in a path (like mkdir -p)
static void mkdirs(const char* path) {
    char tmp[MAX_PATH_LEN];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

// Read an entire file into a malloc'd buffer. Caller frees.
static char* read_file(const char* path, int* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0 || len > 4 * 1024 * 1024) { // 4MB limit
        fclose(f);
        return NULL;
    }

    char* buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);

    if (out_len) *out_len = (int)len;
    return buf;
}

// Find attribute value in an XML tag. Writes to out, returns pointer past value.
// Example: find_attr(str, "full-path", out, 256) finds full-path="value"
static const char* find_attr(const char* xml, const char* attr_name,
                             char* out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "%s=\"", attr_name);

    const char* pos = strstr(xml, search);
    if (!pos) {
        // Try single quotes
        snprintf(search, sizeof(search), "%s='", attr_name);
        pos = strstr(xml, search);
        if (!pos) return NULL;
    }

    pos += strlen(search);
    char quote = *(pos - 1); // matching quote character

    const char* end = strchr(pos, quote);
    if (!end) return NULL;

    size_t len = end - pos;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, pos, len);
    out[len] = '\0';

    return end + 1;
}

// Find tag content: <tag>content</tag> or <tag ...>content</tag>
static const char* find_tag_content(const char* xml, const char* tag,
                                    char* out, size_t out_size) {
    char open[128];
    snprintf(open, sizeof(open), "<%s", tag);

    const char* start = strstr(xml, open);
    if (!start) return NULL;

    // Skip to end of opening tag
    start = strchr(start, '>');
    if (!start) return NULL;
    start++;

    // Find closing tag
    char close[128];
    snprintf(close, sizeof(close), "</%s>", tag);
    const char* end = strstr(start, close);
    if (!end) return NULL;

    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';

    return end + strlen(close);
}

// Get directory portion of a path
static void get_dir(const char* path, char* dir, size_t dir_size) {
    strncpy(dir, path, dir_size - 1);
    dir[dir_size - 1] = '\0';
    char* last_slash = strrchr(dir, '/');
    if (last_slash)
        *last_slash = '\0';
    else
        dir[0] = '\0';
}

// ---- ZIP extraction ----

bool epub_extract(const char* epub_path, const char* cache_dir) {
    unzFile zf = unzOpen(epub_path);
    if (!zf) return false;

    mkdirs(cache_dir);

    char buf[8192];
    int ret = unzGoToFirstFile(zf);

    while (ret == UNZ_OK) {
        unz_file_info fi;
        char filename[MAX_PATH_LEN];
        unzGetCurrentFileInfo(zf, &fi, filename, sizeof(filename),
                              NULL, 0, NULL, 0);

        char fullpath[MAX_PATH_LEN];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", cache_dir, filename);

        // If filename ends with '/', it's a directory
        size_t flen = strlen(filename);
        if (flen > 0 && filename[flen - 1] == '/') {
            mkdirs(fullpath);
        } else {
            // Ensure parent directory exists
            char parent[MAX_PATH_LEN];
            get_dir(fullpath, parent, sizeof(parent));
            if (parent[0]) mkdirs(parent);

            if (unzOpenCurrentFile(zf) == UNZ_OK) {
                FILE* out = fopen(fullpath, "wb");
                if (out) {
                    int bytes;
                    while ((bytes = unzReadCurrentFile(zf, buf, sizeof(buf))) > 0) {
                        fwrite(buf, 1, bytes, out);
                    }
                    fclose(out);
                }
                unzCloseCurrentFile(zf);
            }
        }

        ret = unzGoToNextFile(zf);
    }

    unzClose(zf);
    return true;
}

// ---- OPF parsing ----

// Parse container.xml to find the OPF file path
static bool parse_container(const char* cache_dir, char* opf_path, size_t opf_size) {
    char container_path[MAX_PATH_LEN];
    snprintf(container_path, sizeof(container_path),
             "%s/META-INF/container.xml", cache_dir);

    int len;
    char* xml = read_file(container_path, &len);
    if (!xml) return false;

    bool ok = find_attr(xml, "full-path", opf_path, opf_size) != NULL;
    free(xml);
    return ok;
}

// Parse OPF file for metadata and spine
static bool parse_opf(const char* opf_fullpath, const char* opf_dir_prefix,
                      EpubBook* book) {
    int len;
    char* xml = read_file(opf_fullpath, &len);
    if (!xml) return false;

    // Extract title
    if (!find_tag_content(xml, "dc:title", book->title, MAX_TITLE_LEN)) {
        // Try without namespace prefix
        find_tag_content(xml, "title", book->title, MAX_TITLE_LEN);
    }

    // Extract author
    if (!find_tag_content(xml, "dc:creator", book->author, MAX_TITLE_LEN)) {
        find_tag_content(xml, "creator", book->author, MAX_TITLE_LEN);
    }

    // Default title if not found
    if (book->title[0] == '\0') {
        strncpy(book->title, util_basename(book->filepath), MAX_TITLE_LEN - 1);
    }

    // Build manifest: map id -> href for XHTML items
    // We'll store up to MAX_CHAPTERS manifest items
    typedef struct { char id[128]; char href[MAX_PATH_LEN]; } ManifestItem;
    ManifestItem* manifest = calloc(MAX_CHAPTERS, sizeof(ManifestItem));
    if (!manifest) { free(xml); return false; }
    int manifest_count = 0;

    // Find <manifest> section
    const char* mani_start = strstr(xml, "<manifest");
    const char* mani_end = strstr(xml, "</manifest>");
    if (mani_start && mani_end) {
        const char* pos = mani_start;
        while (pos < mani_end && manifest_count < MAX_CHAPTERS) {
            pos = strstr(pos, "<item");
            if (!pos || pos >= mani_end) break;

            char id[128] = {0}, href[MAX_PATH_LEN] = {0}, media[128] = {0};
            // Find the end of this <item> tag
            const char* tag_end = strchr(pos, '>');
            if (!tag_end || tag_end > mani_end) break;

            find_attr(pos, "id", id, sizeof(id));
            find_attr(pos, "href", href, sizeof(href));
            find_attr(pos, "media-type", media, sizeof(media));

            // Only keep XHTML items
            if (id[0] && href[0] &&
                (strstr(media, "xhtml") || strstr(media, "html"))) {
                strncpy(manifest[manifest_count].id, id, 127);
                strncpy(manifest[manifest_count].href, href, MAX_PATH_LEN - 1);
                manifest_count++;
            }

            pos = tag_end + 1;
        }
    }

    // Parse spine to get reading order
    const char* spine_start = strstr(xml, "<spine");
    const char* spine_end = strstr(xml, "</spine>");
    if (spine_start && spine_end) {
        const char* pos = spine_start;
        while (pos < spine_end && book->chapter_count < MAX_CHAPTERS) {
            pos = strstr(pos, "<itemref");
            if (!pos || pos >= spine_end) break;

            char idref[128] = {0};
            find_attr(pos, "idref", idref, sizeof(idref));

            if (idref[0]) {
                // Look up in manifest
                for (int i = 0; i < manifest_count; i++) {
                    if (strcmp(manifest[i].id, idref) == 0) {
                        int idx = book->chapter_count;
                        // Build full path: cache_dir / opf_dir / href
                        if (opf_dir_prefix[0]) {
                            snprintf(book->chapter_files[idx], MAX_PATH_LEN,
                                     "%s/%s/%s", book->cache_dir,
                                     opf_dir_prefix, manifest[i].href);
                        } else {
                            snprintf(book->chapter_files[idx], MAX_PATH_LEN,
                                     "%s/%s", book->cache_dir, manifest[i].href);
                        }
                        // Use filename as chapter name for now
                        strncpy(book->chapter_names[idx],
                                util_basename(manifest[i].href), MAX_TITLE_LEN - 1);
                        book->chapter_count++;
                        break;
                    }
                }
            }

            pos = strchr(pos, '>');
            if (!pos) break;
            pos++;
        }
    }

    free(manifest);
    free(xml);
    return book->chapter_count > 0;
}

// ---- XHTML to plain text ----

// Decode a single HTML entity starting at &...; Returns chars consumed, writes decoded char
static int decode_entity(const char* src, char* out) {
    if (strncmp(src, "&amp;", 5) == 0)  { *out = '&'; return 5; }
    if (strncmp(src, "&lt;", 4) == 0)   { *out = '<'; return 4; }
    if (strncmp(src, "&gt;", 4) == 0)   { *out = '>'; return 4; }
    if (strncmp(src, "&quot;", 6) == 0)  { *out = '"'; return 6; }
    if (strncmp(src, "&apos;", 6) == 0)  { *out = '\''; return 6; }
    if (strncmp(src, "&nbsp;", 6) == 0)  { *out = ' '; return 6; }
    if (strncmp(src, "&#", 2) == 0) {
        // Numeric entity &#NNN; or &#xHH;
        int val = 0;
        const char* p = src + 2;
        if (*p == 'x' || *p == 'X') {
            p++;
            while (*p && *p != ';') {
                val = val * 16;
                if (*p >= '0' && *p <= '9') val += *p - '0';
                else if (*p >= 'a' && *p <= 'f') val += *p - 'a' + 10;
                else if (*p >= 'A' && *p <= 'F') val += *p - 'A' + 10;
                p++;
            }
        } else {
            while (*p && *p != ';') {
                val = val * 10 + (*p - '0');
                p++;
            }
        }
        if (*p == ';') p++;
        // Simple ASCII range or replace with '?'
        if (val >= 32 && val < 127) {
            *out = (char)val;
        } else if (val == 160) { // non-breaking space
            *out = ' ';
        } else {
            *out = '?';
        }
        return (int)(p - src);
    }
    // Unknown entity - skip the &
    *out = '&';
    return 1;
}

// Check if tag name matches (case-insensitive, stops at space or >)
static bool tag_is(const char* tag_start, const char* name) {
    size_t nlen = strlen(name);
    if (strncasecmp(tag_start, name, nlen) != 0)
        return false;
    char next = tag_start[nlen];
    return next == ' ' || next == '>' || next == '/' || next == '\0';
}

// Convert XHTML to plain text
static char* xhtml_to_text(const char* html, int html_len, int* out_len) {
    // Allocate output buffer (worst case: same size as input)
    char* out = malloc(html_len + 1);
    if (!out) return NULL;

    int oi = 0;          // output index
    bool in_tag = false;
    bool in_body = false;
    bool skip_content = false; // for <style>, <script>
    char tag_name[64];
    int tag_name_len = 0;
    bool tag_is_closing = false;
    bool tag_name_done = false;  // stop collecting after first space
    bool last_was_newline = true; // prevent double newlines

    for (int i = 0; i < html_len; i++) {
        char c = html[i];

        if (c == '<') {
            in_tag = true;
            tag_name_len = 0;
            tag_is_closing = false;
            tag_name_done = false;
            continue;
        }

        if (in_tag) {
            if (c == '>') {
                in_tag = false;
                tag_name[tag_name_len] = '\0';

                // Check for body tag
                if (tag_is(tag_name, "body"))
                    in_body = true;
                if (tag_is(tag_name, "/body"))
                    in_body = false;

                // Skip style/script content
                if (tag_is(tag_name, "style") || tag_is(tag_name, "script"))
                    skip_content = true;
                if (tag_is(tag_name, "/style") || tag_is(tag_name, "/script"))
                    skip_content = false;

                if (!in_body || skip_content)
                    continue;

                // Block-level tags -> newline
                const char* name = tag_is_closing ? tag_name + 1 : tag_name;
                if (tag_is(name, "p") || tag_is(name, "div") ||
                    tag_is(name, "br") || tag_is(name, "br/") ||
                    tag_is(name, "h1") || tag_is(name, "h2") ||
                    tag_is(name, "h3") || tag_is(name, "h4") ||
                    tag_is(name, "h5") || tag_is(name, "h6") ||
                    tag_is(name, "li") || tag_is(name, "tr") ||
                    tag_is(name, "blockquote")) {

                    if (!last_was_newline && oi > 0) {
                        out[oi++] = '\n';
                        last_was_newline = true;
                    }
                    // Extra newline after headings and paragraphs (closing)
                    if (tag_is_closing &&
                        (tag_is(name, "p") || tag_is(name, "h1") ||
                         tag_is(name, "h2") || tag_is(name, "h3"))) {
                        out[oi++] = '\n';
                    }
                }

                continue;
            }

            // Build tag name (only collect up to first space/tab/newline)
            if (!tag_name_done) {
                if (tag_name_len == 0 && c == '/') {
                    tag_is_closing = true;
                    tag_name[tag_name_len++] = c;
                } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    tag_name_done = true;
                } else if (tag_name_len < 63) {
                    tag_name[tag_name_len++] = c;
                }
            }
            continue;
        }

        // Outside tags - collect text
        if (!in_body || skip_content)
            continue;

        if (c == '&') {
            char decoded;
            int consumed = decode_entity(&html[i], &decoded);
            out[oi++] = decoded;
            i += consumed - 1; // -1 because loop increments
            last_was_newline = false;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            // Collapse whitespace
            if (!last_was_newline && oi > 0 && out[oi - 1] != ' ') {
                out[oi++] = ' ';
            }
        } else {
            out[oi++] = c;
            last_was_newline = (c == '\n');
        }
    }

    out[oi] = '\0';

    // Trim trailing whitespace
    while (oi > 0 && (out[oi - 1] == ' ' || out[oi - 1] == '\n')) {
        out[--oi] = '\0';
    }

    if (out_len) *out_len = oi;
    return out;
}

// ---- Public API ----

bool epub_open(const char* epub_path, EpubBook* book) {
    memset(book, 0, sizeof(EpubBook));
    strncpy(book->filepath, epub_path, MAX_PATH_LEN - 1);

    // Create cache directory based on hash of filename
    unsigned int hash = util_hash_string(epub_path);
    snprintf(book->cache_dir, MAX_PATH_LEN, "%s/%08x", CACHE_DIR, hash);

    // Extract ZIP
    if (!epub_extract(epub_path, book->cache_dir))
        return false;

    // Find OPF path from container.xml
    char opf_rel[MAX_PATH_LEN] = {0};
    if (!parse_container(book->cache_dir, opf_rel, sizeof(opf_rel)))
        return false;

    // Get OPF directory (for resolving relative hrefs)
    get_dir(opf_rel, book->opf_dir, MAX_PATH_LEN);

    // Build full OPF path
    char opf_full[MAX_PATH_LEN];
    snprintf(opf_full, sizeof(opf_full), "%s/%s", book->cache_dir, opf_rel);

    // Parse OPF
    return parse_opf(opf_full, book->opf_dir, book);
}

bool epub_load_chapter(EpubBook* book, int chapter_index, ChapterContent* content) {
    if (chapter_index < 0 || chapter_index >= book->chapter_count)
        return false;

    memset(content, 0, sizeof(ChapterContent));

    int html_len;
    char* html = read_file(book->chapter_files[chapter_index], &html_len);
    if (!html) return false;

    content->text = xhtml_to_text(html, html_len, &content->length);
    free(html);

    if (!content->text) return false;

    // If chapter produced no text (e.g. cover page with only images),
    // provide placeholder so the reader doesn't fail entirely
    if (content->length == 0) {
        free(content->text);
        content->text = strdup("[No text content]");
        content->length = strlen(content->text);
    }

    return true;
}

void epub_free_chapter(ChapterContent* content) {
    if (content->text) {
        free(content->text);
        content->text = NULL;
    }
    content->length = 0;
}
