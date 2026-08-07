#include "main.h"
#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "parser.h"


void compile(const SV* restrict source, const SV* restrict source_filename);

int main(const int argc, char* argv[]) {
    SVList list = SVList_new();
    for (int i=0; i<argc ; i++) {
        const size_t len = strlen(argv[i]);
        SVList_push(&list, (SV) {argv[i], len});
    }

    for (SVListView list_v = SVList_to_view(&list); SVListView_has_next(&list_v) ;) {
        const SV* item = SVListView_next(&list_v);
        printf("arg: '"SV_format"'\n", SV_p_args(item));
    }

    SVList_free(&list);

    const char* raw_source_filename = "/home/frantisek/Projects/KlepCompiler/a.klep";
    const SV source_filename = SV_from_str(raw_source_filename);

    FILE* source_file = fopen(raw_source_filename, "rb");
    if (source_file == NULL) {
        perror("Failed to open file");
        exit(1);
    }


    // Seek to the end of the file
    if (fseek(source_file, 0, SEEK_END) != 0) {
        fclose(source_file);
        fprintf(stderr, "failed file read\n");
        exit(1);
    }

    const long source_size = ftell(source_file);

    if (fseek(source_file, 0, SEEK_SET) != 0) {
        fclose(source_file);
        fprintf(stderr, "failed file read\n");
        exit(1);
    }

    char* raw_source = malloc(source_size+1);
    fread(raw_source, 1, source_size, source_file);
    raw_source[source_size] = '\0';

    fclose(source_file);

    // printf("source:\n");
    // printf("%s", raw_source);
    // printf("\n\n");

    const SV test_source = SV_from_str_len(raw_source, source_size);

    printf("[info] file loaded\n");

    compile(&test_source, &source_filename);

    free(raw_source);
    return 0;
}

void compile(const SV* restrict source, const SV* restrict source_filename) {
    SVList tokens = p_tokenize(source);

    // for (SVListView tokens_iter = SVList_to_view(&tokens); SVListView_has_next(&tokens_iter) ;) {
    //     const SV* item = SVListView_next(&tokens_iter);
    //     printf("'"SV_format"'\n", SV_p_args(item));
    // }

    parse(&tokens, source, source_filename);


    SVList_free(&tokens);
}
