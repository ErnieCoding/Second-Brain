/*
Implementation of a simple getenv process.
*/
#include "getenv.h"
using namespace std;

ifstream get_file(const string& filepath)
{
    ifstream file(filepath);
    if(!file.is_open()){
        cerr << "Error: cannot open environment file at location " << filepath << endl;
        return ifstream{};
    }

    return file;
}

map<string, const char*> load_env(ifstream& file)
{
    map<string, const char*> env_vars;

    string line;
    while(getline(file, line)) {
        size_t equals_pos = line.find('=');

        if(equals_pos!=string::npos) {
            string key = line.substr(0, equals_pos);
            string value = line.substr(equals_pos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            const char* final_value = value.c_str();

            env_vars[key] = final_value;
        }
    }

    return env_vars;
}