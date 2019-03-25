
#include "tcg-plugin.h"
#include "wycinwyc.h"

vector* mappings; // mappings.item.memory_range

target_ulong printf_addr;
target_ulong fprintf_addr;
target_ulong dprintf_addr;
target_ulong sprintf_addr; 
target_ulong snprintf_addr;
target_ulong malloc_addr;
target_ulong malloc_r_addr;
target_ulong realloc_addr;
target_ulong realloc_r_addr;
target_ulong free_addr;
target_ulong free_r_addr;
target_ulong calloc_addr;


// vector *load_json(const char *filename)
// {
// 	// read data from file
// 	FILE *fp = fopen(filename, "r");
// 	if (fp == NULL)
// 	{
// 		printf("file %s open failed.\n", filename);
// 		return 0;
// 	}
// 	fseek(fp, 0, SEEK_END);
// 	int filesize = ftell(fp);
// 	// printf("filesize %d\n", filesize);
// 	char *filedata = NULL;

// 	fseek(fp, 0, SEEK_SET);

// 	filedata = (char *)malloc(filesize + 1);
// 	fgets(filedata, filesize + 1, fp);
// 	// printf("%s\n", filedata);
// 	fclose(fp);

// 	// map json to struct memory_mapping
// 	vector *root = (vector *)malloc(sizeof(vector));
// 	root->next = NULL;
// 	vector *pre_node = root;

// 	cJSON *pJson = cJSON_Parse(filedata);
// 	cJSON *mem_map = cJSON_GetObjectItem(pJson, "memory_mapping");

// 	int n = cJSON_GetArraySize(mem_map);
// 	for (int i = 0; i < n; i++)
// 	{
// 		cJSON *mem = cJSON_GetArrayItem(mem_map, i);

// 		int address = cJSON_GetObjectItem(mem, "address")->valueint;
// 		// printf("address %d\n", address);
// 		int size = cJSON_GetObjectItem(mem, "size")->valueint;
// 		char *perms_str = cJSON_GetObjectItem(mem, "permissions")->valuestring;
// 		char perms = 0;
// 		perms += perms_str[0] == 'r' ? 1 : 0;
// 		perms += perms_str[1] == 'w' ? 2 : 0;
// 		perms += perms_str[2] == 'x' ? 4 : 0;
// 		// printf("perms %s %d\n", perms_str, perms);
// 		//bool file_backed;
// 		int file_backed = cJSON_GetObjectItem(mem, "file") == NULL ? 0 : 1;
// 		// printf("file_backed %d\n", file_backed);
// 		pre_node->next = (vector *)malloc(sizeof(vector));
// 		if (pre_node->next != NULL)
// 		{
//             vector *new_node = pre_node->next;
//             memory_range new_mr;
//             new_mr.address = address;
//             new_mr.size = size;
//             new_mr.perms = perms;
//             new_mr.file_backed = file_backed;
//             vector_item vit1;
//             vit1.mr = new_mr;
//             new_node->item = vit1;
// 			new_node->next = NULL;
// 			pre_node = new_node;
// 		}
// 	}

// 	vector *ret = root->next;
// 	free(root);
//     BubbleSortVector(ret);
//     // PrintVector(ret);
// 	return ret;
// }

// int main()
// {
// 	printf("Version: %s\n", cJSON_Version());

// 	vector *vmr = load_json("conf.json");
// 	while (vmr != NULL)
// 	{
// 		printf("%d, %d, %d, %d\n", vmr->item.mr.address, vmr->item.mr.size, vmr->item.mr.perms, vmr->item.mr.file_backed);
// 		vmr = vmr->next;
// 	}
// }