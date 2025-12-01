#include <git2.h>
using namespace std;

int git_libgit2_init();

git_repository* repo = NULL;

int open_existing_repo(git_repository* repo, const char* path);

