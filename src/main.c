#include "main.h"
#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "args.h"
#include "compiler.h"
#include "passes/mir_process.h"
#include "passes/parser/parser.h"
#include "passes/typechecking/typechecking.h"






int main(const int argc, char* argv[]) {
    SVList args_list = SVList_new();
    for (int i = 0; i < argc; i++) {
        const size_t len = strlen(argv[i]);
        SVList_push(&args_list, (SV){argv[i], len});
    }

    const SV program_name = args_list.array[0];
    const CompilerArgs args = parse_args(&args_list, program_name);

    SVList_free(&args_list);

    const char* raw_source_filename = args.raw_input_filename;
    const SV source_filename = args.input_filename;

    // printf("raw_source_filename = '%s'\n", raw_source_filename);

    FILE* source_file = fopen(raw_source_filename, "rb");
    if (source_file == NULL) {
        perror("Failed to open file");
        exit(1);
    }


    // Seek to the end of the file
    if (fseek(source_file, 0, SEEK_END) != 0) {
        fclose(source_file);
        fprintf(stderr, "Failed file read\n");
        exit(1);
    }

    const long source_size = ftell(source_file);

    if (fseek(source_file, 0, SEEK_SET) != 0) {
        fclose(source_file);
        fprintf(stderr, "Failed file read\n");
        exit(1);
    }

    char* raw_source = malloc(source_size+1);
    fread(raw_source, 1, source_size, source_file);
    raw_source[source_size] = '\0';

    fclose(source_file);

    const SV source = SV_from_str_len(raw_source, source_size);

    printf("[info] file loaded\n");

    const TargetInfo target_info = (TargetInfo) {
        8,
        8,
        8
    };

    const SourceInfo source_info = (SourceInfo) {
        .source = source,
        .source_filename = source_filename
    };


    main_compile(target_info, source_info, &args);

    free(raw_source);
    return 0;
}
