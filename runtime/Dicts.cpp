#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

struct DictEntry {
    bool is_string;
    union {
        char* s;
        uint64_t i;
    } key;
    uint64_t value;
};

struct GSPPDict {
    std::vector<DictEntry> entries;
};

extern "C" {

GSPPDict* gspp_dict_new() {
    return new GSPPDict();
}

static bool is_string_ptr(uint64_t val) {
    return val > 0x10000;
}

void gspp_dict_set(GSPPDict* dict, uint64_t key, uint64_t val) {
    bool is_str = is_string_ptr(key);
    for (auto& entry : dict->entries) {
        if (entry.is_string == is_str) {
            if (is_str) {
                if (strcmp(entry.key.s, (char*)key) == 0) {
                    entry.value = val;
                    return;
                }
            } else {
                if (entry.key.i == key) {
                    entry.value = val;
                    return;
                }
            }
        }
    }
    DictEntry e;
    e.is_string = is_str;
    if (is_str) {
        e.key.s = strdup((char*)key);
    } else {
        e.key.i = key;
    }
    e.value = val;
    dict->entries.push_back(e);
}

uint64_t gspp_dict_get(GSPPDict* dict, uint64_t key) {
    bool is_str = is_string_ptr(key);
    for (auto& entry : dict->entries) {
        if (entry.is_string == is_str) {
            if (is_str) {
                if (strcmp(entry.key.s, (char*)key) == 0) return entry.value;
            } else {
                if (entry.key.i == key) return entry.value;
            }
        }
    }
    return 0;
}

uint64_t gspp_dict_get_default(GSPPDict* dict, uint64_t key, uint64_t default_val) {
    uint64_t res = gspp_dict_get(dict, key);
    if (res == 0) {
        // This is a bit problematic if 0 is a valid value,
        // but for now our NULL/nil/0 are conflated.
        return default_val;
    }
    return res;
}

void gspp_dict_remove(GSPPDict* dict, uint64_t key) {
    bool is_str = is_string_ptr(key);
    for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it) {
        if (it->is_string == is_str) {
            if (is_str) {
                if (strcmp(it->key.s, (char*)key) == 0) {
                    free(it->key.s);
                    dict->entries.erase(it);
                    return;
                }
            } else {
                if (it->key.i == key) {
                    dict->entries.erase(it);
                    return;
                }
            }
        }
    }
}

uint64_t gspp_dict_pop(GSPPDict* dict, uint64_t key) {
    uint64_t val = gspp_dict_get(dict, key);
    gspp_dict_remove(dict, key);
    return val;
}

void gspp_dict_clear(GSPPDict* dict) {
    for (auto& entry : dict->entries) {
        if (entry.is_string) free(entry.key.s);
    }
    dict->entries.clear();
}

struct GSPPList {
    uint64_t* data;
    long long len;
    long long cap;
};
extern "C" GSPPList* gspp_list_new(long long initial_cap);
extern "C" void gspp_list_append(GSPPList* list, uint64_t val);

GSPPList* gspp_dict_keys(GSPPDict* dict) {
    GSPPList* list = gspp_list_new(dict->entries.size());
    for (auto& entry : dict->entries) {
        gspp_list_append(list, entry.is_string ? (uint64_t)entry.key.s : entry.key.i);
    }
    return list;
}

GSPPList* gspp_dict_values(GSPPDict* dict) {
    GSPPList* list = gspp_list_new(dict->entries.size());
    for (auto& entry : dict->entries) {
        gspp_list_append(list, entry.value);
    }
    return list;
}

long long gspp_dict_len(GSPPDict* dict) {
    return (long long)dict->entries.size();
}

GSPPDict* gspp_dict_union(GSPPDict* d1, GSPPDict* d2) {
    GSPPDict* res = new GSPPDict(*d1);
    for (auto& entry : d2->entries) {
        gspp_dict_set(res, entry.is_string ? (uint64_t)entry.key.s : entry.key.i, entry.value);
    }
    return res;
}

GSPPDict* gspp_dict_intersection(GSPPDict* d1, GSPPDict* d2) {
    GSPPDict* res = new GSPPDict();
    for (auto& entry : d1->entries) {
        uint64_t key = entry.is_string ? (uint64_t)entry.key.s : entry.key.i;
        for (auto& e2 : d2->entries) {
            bool is_match = false;
            if (entry.is_string == e2.is_string) {
                if (entry.is_string) {
                    if (strcmp(entry.key.s, e2.key.s) == 0) is_match = true;
                } else {
                    if (entry.key.i == e2.key.i) is_match = true;
                }
            }
            if (is_match) {
                gspp_dict_set(res, key, entry.value);
                break;
            }
        }
    }
    return res;
}

}
