#define HTTPSERVER_IMPL
#include "httpserver.h"

#include <git2.h>
#include <stdio.h>

#define PORT 8080
#define REPO_PATH "."

git_repository* repo = NULL;

/* TEMPLATES */
char** base_template;
char** index_template;
char** repo_template;

void initialize_templates() {
    base_template = template_new("./templates/base.html");
    index_template = template_new("./templates/index.html");
    repo_template = template_new("./templates/repo.html");
}

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

struct http_response_s* repo_endpoint() {
    // Get the HEAD reference
    git_reference* head_ref = NULL;
    git_repository_head(&head_ref, repo);
    // Get the commit object of the HEAD reference
    git_oid head_oid = *git_reference_target(head_ref);
    commit_array* commits = get_commit_messages(&head_oid);

    struct http_response_s* response = http_response_init();
    http_response_status(response, 200);
    http_response_header(response, "Content-Type", "text/html");
    char* context[] = {"", repo_template[0], NULL};
    char* text = template_render(base_template, context);

    char buf[1000];
    strcat(text, "<table><thead><tr><th>Commit ID</th><th>Commit Message</th></tr></thead><tbody>");
    for (int i = 0; i < commits->size; i++) {
        sprintf(buf, "<tr><td>%s</td><td>%s</td></tr>", commits->commits[i].id, commits->commits[i].message);
        strcat(text, buf);
    }
    strcat(text, "</tbody></table>");

    http_response_body(response, text, strlen(text));

    // Free the memory
    for (int i = 0; i < commits->size; ++i) {
        free(commits->commits[i].id);
        free(commits->commits[i].message);
    }
    free(commits);
    free(head_ref);
    /* free(text); */ // causes memory corruption (!?) (C ...)

    return response;
}

void handle_request(struct http_request_s* request) {
    char* url = http_request_path(request);

    struct http_response_s* response;
    if (strcmp(url, "/") == 0) {
        response = root_endpoint();
    } else if (strcmp(url, "/repo") == 0) {
        response = repo_endpoint();
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
