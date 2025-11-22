#include <iostream>
#include "getenv.h"
using namespace std;

int main()
{
    string filepath;
    cout << "Enter filepath to your environment file: " << endl;
    cin >> filepath;

    ifstream file = get_file(filepath);
    map<string, string> env = load_env(file);

    cout << "The path to your obsidian vault is: " << env.at("REPO_PATH") << endl;
}