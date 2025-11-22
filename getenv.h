#ifndef GETENV_H
#define GETENV_H

#include <string>
#include <map>
#include <fstream>
#include <iostream>

using namespace std;

ifstream get_file(const string& filepath);

map<string, string> load_env(ifstream& file);

#endif