#ifndef REGISTRY_H
#define REGISTRY_H
#ifdef __cplusplus
extern "C" {
#endif
long RegOpenKeyExA(long key, char* subkey, long reserved, long access, int* newkey);
long RegCloseKey(long key);
long RegQueryValueExA(long key, char* value, int* reserved, int* type, int* data, int* count);
long RegCreateKeyExA(long key, char* name, long reserved,
							   void* classs, long options, long security,
							   void* sec_attr, int* newkey, int* status) ;
long RegSetValueExA(long key, char* name, long v1, long v2, void* data, long size);
#ifdef __cplusplus
};
#endif
#endif