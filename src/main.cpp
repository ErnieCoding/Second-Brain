#include <iostream>
#include <git2.h>
#include "getenv.h"
using namespace std;

int open_existing_repo(git_repository** repo, const char* path){
    int error = git_repository_open(repo, path);

    return error;
}

int main()
{
    git_libgit2_init(); // initialize libgit2

    git_repository* repo = NULL; // initialize empty repo

    string filepath;
    cout << "Enter filepath to your environment file: " << endl;
    cin >> filepath;

    ifstream file = get_file(filepath);
    map<string, const char*> env = load_env(file);

    cout << "The path to your obsidian vault is: " << env.at("REPO_PATH") << endl;

    int status = open_existing_repo(&repo, env.at("REPO_PATH"));
    cout << "status: " << status << endl;

    git_repository_free(repo);
}