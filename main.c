#define HTTPSERVER_IMPL
#include "httpserver.h"

#include <git2.h>
#include <stdio.h>
#include <dirent.h>

#define PORT 8080
#define REPO_PATH "."

/* TEMPLATES */

char** base_template;
char** index_template;

void initialize_templates() {
    base_template = template_new("./templates/base.html");
    index_template = template_new("./templates/index.html");
}

/* EXTRACTORS + HELPERS */

git_repository* repo = NULL;

typedef struct {
    char* id;
    char* message;
} commit_data;

typedef struct {
    commit_data* commits;
    size_t size;
} commit_array;

commit_array* get_commit_messages(git_oid *commit_oid) {
    git_commit *commit = NULL;
    git_commit_lookup(&commit, repo, commit_oid);

    commit_array *commits = NULL;

    if (commit != NULL) {
        commits = malloc(sizeof(commit_array));
        commits->commits = malloc(sizeof(commit_data));
        commits->size = 1;

        commits->commits[0].id = malloc((strlen(git_oid_tostr_s(commit_oid)) + 1) * sizeof(char));
        strcpy(commits->commits[0].id, git_oid_tostr_s(commit_oid));

        commits->commits[0].message = malloc((strlen(git_commit_message(commit)) + 1) * sizeof(char));
        strcpy(commits->commits[0].message, git_commit_message(commit));

        // Traverse parent commits recursively
        size_t parent_count = git_commit_parentcount(commit);
        for (size_t i = 0; i < parent_count; ++i) {
            git_oid parent_oid = *git_commit_parent_id(commit, i);
            commit_array *parent_info = get_commit_messages(&parent_oid);

            // Reallocate memory for commits array to hold parent commits
            commits->commits = realloc(commits->commits, (commits->size + parent_info->size) * sizeof(commit_data));

            // Copy parent commits into commits array
            memcpy(commits->commits + commits->size, parent_info->commits, parent_info->size * sizeof(commit_data));

            // Update size
            commits->size += parent_info->size;

            // Free parent_info after using it
            free(parent_info->commits);
            free(parent_info);
        }

        git_commit_free(commit);
    }

    return commits;
}

char* read_to_string(const char* file_path) {
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = malloc(file_size + 100);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';

    fclose(file);
    return content;
}

char* get_readme() {
    // TODO: render readme
    // TODO: case insensitive readme filename
    char* readme_content = read_to_string("README.md");
    if (!readme_content) {
        return "README.md file not found";
    }
    return readme_content;
}

char* get_file_tree() {
    struct dirent *de;

    DIR* dr = opendir("."); // current directory
    if (!dr) {
        return NULL;
    }

    char* file_tree = malloc(100000);
    strcat(file_tree, "<h2>File Tree</h2><pre><ul>");

    while ((de = readdir(dr)) != NULL) {
        if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
            char tmp[100];
            sprintf(tmp, "<li><a href=\"/file?%s\">", de->d_name);
            strcat(file_tree, tmp);
            strcat(file_tree, de->d_name);
            strcat(file_tree, "</a></li>");
        }
    }

    strcat(file_tree, "</ul></pre>");

    closedir(dr);
    return file_tree;
}

git_commit* get_commit(char* commit_id) {
    git_commit* commit;
    git_oid oid;
    git_oid_fromstr(&oid, commit_id);
    git_commit_lookup(&commit, repo, &oid);
    return commit;
}

typedef struct {
    char* diff_string;
    size_t len;
} diff_payload;

int print_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload) {
    (void)delta; (void)hunk; // Unused parameters

    diff_payload *dp = (diff_payload*)payload;
    dp->diff_string = realloc(dp->diff_string, dp->len + line->content_len + 1);
    memcpy(dp->diff_string + dp->len, line->content, line->content_len);
    dp->len += line->content_len;
    dp->diff_string[dp->len] = '\0';

    return 0;
}

/* ENDPOINTS */

struct http_response_s* root_endpoint() {
    struct http_response_s* response = http_response_init();
    http_response_status(response, 200);
    http_response_header(response, "Content-Type", "text/html");
    char* context[] = {"", index_template[0], NULL};
    char* text = template_render(base_template, context);
    http_response_body(response, text, strlen(text));
    /* free(text); */ // causes memory corruption (!?) (C ...)
    return response;
}

struct http_response_s* file_endpoint(http_string_t query_str) {
    struct http_response_s* response = http_response_init();
    http_response_status(response, 200);
    http_response_header(response, "Content-Type", "text/html");
    char* file_name = query_str.buf;
    char* file_content = read_to_string(file_name);
    char* text = malloc(100000);
    strcat(text, "<h2>");
    strcat(text, file_name);
    strcat(text, "</h2>");
    strcat(text, "<pre>");
    strcat(text, file_content);
    strcat(text, "</pre>");
    char* file_tree = get_file_tree();
    strcat(text, file_tree);
    char* context[] = {"", text, NULL};
    char* resp_body = template_render(base_template, context);
    http_response_body(response, resp_body, strlen(resp_body));
    return response;
}

struct http_response_s* commit_endpoint(http_string_t query_str) {
    struct http_response_s* response = http_response_init();
    http_response_status(response, 200);
    http_response_header(response, "Content-Type", "text/html");

    char* commit_id = query_str.buf;
    git_commit* commit = get_commit(commit_id);
    char* text = malloc(100000);
    strcat(text, "<h2>");
    strcat(text, commit_id);
    strcat(text, "</h2>");

    const git_signature* commit_author = git_commit_author(commit);
    char tmp[100];
    sprintf(tmp, "<h3>Author: %s</h3>\n", commit_author->name);
    strcat(text, tmp);

    git_commit* parent;
    git_commit_parent(&parent, commit, 0);

    git_diff* diff;
    git_tree* commit_tree, *parent_tree;
    git_commit_tree(&commit_tree, commit);
    git_commit_parent(&parent, commit, 0);
    git_commit_tree(&parent_tree, parent);
    git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, NULL);

    diff_payload dp = {NULL, 0};
    git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, print_cb, &dp);
    strcat(text, "<pre>");
    strcat(text, dp.diff_string);
    strcat(text, "</pre>");

    git_commit_free(commit);
    git_commit_free(parent);
    git_diff_free(diff);
    free(dp.diff_string);

    char* context[] = {"", text, NULL};
    char* resp_body = template_render(base_template, context);
    http_response_body(response, resp_body, strlen(resp_body));
    return response;
}

struct http_response_s* repo_endpoint() {
    // Get the HEAD reference
    git_reference* head_ref = NULL;
    git_repository_head(&head_ref, repo);
    // Get the commit object of the HEAD reference
    git_oid head_oid = *git_reference_target(head_ref);
    commit_array* commits = get_commit_messages(&head_oid);

    char* text = malloc(100000);

    struct http_response_s* response = http_response_init();
    http_response_status(response, 200);
    http_response_header(response, "Content-Type", "text/html");

    char* readme_content = get_readme();
    strcat(text, "<h2>README</h2><pre>");
    strcat(text, readme_content);
    strcat(text, "</pre>");

    char* file_tree = get_file_tree();
    strcat(text, file_tree);

    char buf[1000];
    strcat(text, "<h2>Commit History</h2><table><thead><tr><th>Commit ID</th><th>Commit Message</th></tr></thead><tbody>");
    for (int i = 0; i < commits->size; i++) {
        sprintf(buf, "<tr><td><a href=\"/commit?%s\">%s</a></td><td>%s</td></tr>", commits->commits[i].id, commits->commits[i].id, commits->commits[i].message);
        strcat(text, buf);
    }
    strcat(text, "</tbody></table>");

    // Free the memory
    for (int i = 0; i < commits->size; ++i) {
        free(commits->commits[i].id);
        free(commits->commits[i].message);
    }
    free(commits);
    free(head_ref);
    /* free(text); */ // causes memory corruption (!?) (C ...)

    char* context[] = {"", text, NULL};
    char* resp_body = template_render(base_template, context);
    http_response_body(response, resp_body, strlen(resp_body));

    return response;
}

void trim_querystring(char* s) {
    strtok(s, "?");
}

/* SERVER CODE */

void handle_request(struct http_request_s* request) {
    char* url = http_request_path(request);
    struct http_string_s query_str = http_request_querystring(request);

    trim_querystring(url);

    struct http_response_s* response;
    if (strcmp(url, "/") == 0) {
        response = root_endpoint();
    } else if (strcmp(url, "/repo") == 0) {
        response = repo_endpoint();
    } else if (strcmp(url, "/file") == 0) {
        response = file_endpoint(query_str);
    } else if (strcmp(url, "/commit") == 0) {
        response = commit_endpoint(query_str);
    } else {
        response = http_quick_response(404, "404 not found!");
    }

    http_respond(request, response);
}

int main(int argc, char* argv[]) {
    initialize_templates();

    git_libgit2_init();
    int error = git_repository_open(&repo, REPO_PATH);
    if (error < 0) {
        const git_error* e = giterr_last();
        printf("Error %d: %s\n", error, e->message);
        git_libgit2_shutdown();
        return 1;
    }

    struct http_server_s* server = http_server_init(PORT, handle_request);
    printf("Server listening on port: %i\n", PORT);
    http_server_listen(server);

    git_repository_free(repo);
    git_libgit2_shutdown();

    return 0;
}
