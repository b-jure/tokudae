#define tokuc_c

#include "tokudaeprefix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "tokudae.h"
#include "tokudaeaux.h"
#include "tokudaelimits.h"


#if !defined(TOKU_PROGNAMEC)
#define TOKU_PROGNAMEC  "tokuc"                 /* default program name */
#define TOKU_OUTPUTC    TOKU_PROGNAMEC ".out"   /* default output file name */
#endif


static int32_t dump = 1;                            /* dump bytecode? */
static int32_t list = 0;                            /* list bytecode? */
static int32_t strip = 0;                           /* strip debug info? */
static char verbosity = 0;                      /* verbosity level */
static char showdesc = 0;                       /* show opcode description? */
static const char *progname = TOKU_PROGNAMEC;   /* actual program name */
static char doutput[] = { TOKU_OUTPUTC };       /* default output file name */
static const char *output = doutput;            /* actual output file name */


#define EQ(l,r)         (strcmp(l, r) == 0)


static void usage(void) {
    printf(
    "usage: %s [options] [filenames]\n"
    "Available options are:\n"
    "   -l n        list opcodes according to 'n' (default 0, max 3)\n"
    "   -D          show opcode description in opcode listing ('-l')\n"
    "   -o name     output to file 'name' (default is \"%s\")\n"
    "   -p          parse only\n"
    "   -s          strip debug information\n"
    "   -v          show version information\n"
    "   -h          show help (this)\n"
    "   --          stop handling options\n"
    "   -           stop handling options and process stdin\n",
    progname, doutput);
}


static void _fatal(int32_t usg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", progname);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    if (usg) usage();
    exit(EXIT_FAILURE);
}


#define fatal(msg)          _fatal(0, msg)
#define fatalf(fmt,...)     _fatal(0, fmt, __VA_ARGS__)
#define ufatal(msg)         _fatal(1, msg)
#define ufatalf(fmt,...)    _fatal(1, fmt, __VA_ARGS__)


static const char *getarg(char *v[], int32_t *i, int32_t j, const char *opt) {
    const char *arg;
    if (v[*i][j+1] == '\0') {
        *i += 1; /* go to next arg */
        if (!v[*i] || v[*i][0] == '\0') goto err;
        arg = v[*i];
    } else
        arg = &v[*i][j+1];
    if (arg[0] == '-' && arg[1] != '\0')
        goto err;
    return arg;
err:
    ufatalf("option '%s' needs argument", opt);
    return NULL; /* to prevent warnings */
}


#define checkrest()     if ((arg)[j+1] != 0) { j++; goto read_opt; }
#define pversion()      printf("%s\n", TOKU_COPYRIGHT);

static int32_t cliargs(int32_t argc, char *argv[]) {
    int32_t i, version = 0;
    for (i=1; i<argc; i++) {
        int32_t j = 0;
        const char *arg = argv[i];
        if (arg[0] != '-') break; /* end of options */
    read_opt:
        switch (arg[j]) {
            case 'l': {
                arg = getarg(argv, &i, j, "-l");
                if (strlen(arg) > 1 || *arg < '0' || *arg > '3')
                    fatalf("invalid 'n' ('%s') for '-l', expected [0, 3]",arg);
                verbosity = *arg - '0';
                list = 1;
                break;
            }
            case 'o': {
                output = getarg(argv, &i, j, "-o");
                if (EQ(output, "-"))
                    output = NULL;
                break;
            }
            case 'D': showdesc = 1; checkrest(); break;
            case 'p': dump = 0; checkrest(); break;
            case 's': strip = 1; checkrest(); break;
            case 'v': ++version; checkrest(); break;
            case 'h': {
                if (version) pversion();
                usage();
                exit(EXIT_SUCCESS);
                break; /* to avoid warnings */
            }
            case '-': {
                if (EQ(arg, "-") || EQ(arg, "--")) {
                    i++;
                    if (version) ++version;
                    goto end; /* no more arguments */
                }
                toku_assert(j == 0);
                j++; /* skip '-' */
                goto read_opt;
            }
            default: {
                toku_assert(j > 0);
                ufatalf("invalid option '%s%s'", j==1 ? "-" : "", arg);
            }
        }
    }
end:
    if (version) {
        pversion();
        if (version == argc-1)
            exit(EXIT_SUCCESS);
    }
    if (i == argc && (list || !dump)) { /* no files? */
        dump = 0; /* do not dump */
        argv[--i] = doutput; /* set default output as filename */
    }
    return i;
}


#define S(x)    (((x) == 1) ? "" : "s")

static void list_header(const toku_Cinfo *ci) {
    const char *what = (ci->defline == 0) ? "main" : "function";
    const char *src = ci->source;
    if (*src == '=' || *src == '@')
        src++;
    else if (*src == TOKU_SIGNATURE[0])
        src = "(bstring)";
    else
        src = "(string)";
    printf("%s <%s:%d,%d> (%d bytes of code at %p)\n",
            what, src, ci->defline, ci->lastdefline, ci->ncode, ci->code);
    printf("%d%s parameter%s, %d slot%s, %d upvalue%s, ",
            ci->nparams, ci->isvararg?"+":"", S(ci->nparams),
            ci->nslots, S(ci->nslots),
            ci->nupvals, S(ci->nupvals));
    printf("%d local%s, %d constant%s, %d function%s\n",
            ci->nlocals, S(ci->nlocals),
            ci->nconstants, S(ci->nconstants),
            ci->nfunctions, S(ci->nfunctions));
}


static void list_opcode(toku_State *T, toku_Cinfo *ci, int32_t verb) {
    int32_t i = 0;
    toku_Opcode opc;
    if (tokui_unlikely(!toku_getopcode(T, ci, 0, &opc))) /* no bytecode? */
        return; /* (this should not happen) */
    do {
        printf("\t%d\t", opc.offset);
        if (opc.line > 0)
            printf("[%d]\t", opc.line);
        else
            printf("[-]\t");
        printf("%-16s\t", opc.name);
        if (verb >= 1) { /* show opcode arguments? */
            for (int32_t j=0; cast_u32(j)<t_arraysize(opc.args); j++) {
                if (opc.args[j] == -1)
                    printf("- ");
                else
                    printf("%d ", opc.args[j]);
            }
        }
        if (showdesc) { /* show opcode description? */
            toku_Opdesc opd;
            toku_getopdesc(T, &opd, &opc);
            printf("    # %s", opd.desc);
        }
        printf("\n");
        i++;
    } while (toku_getopcode_next(T, &opc));
}


static void printType(toku_State *T, int32_t t) {
    switch (t) {
        case TOKU_T_NIL: printf("N"); break;
        case TOKU_T_BOOL: printf("B"); break;
        case TOKU_T_STRING: printf("S"); break;
        case TOKU_T_NUMBER: {
            if (toku_is_integer(T, -1)) /* number is integer? */
                printf("I");
            else /* otherwise float */
                printf("F");
            break;
        }
        default: toku_assert(0); /* unreachable */
    }
}


static void printString(const char *s, size_t l) {
    printf("\"");
    for (size_t i=0; i<l; i++) {
        int32_t c = cast_i32(cast_u8(s[i]));
        switch (c) {
            case '"': printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\a': printf("\\a"); break;
            case '\b': printf("\\b"); break;
            case '\f': printf("\\f"); break;
            case '\n': printf("\\n"); break;
            case '\r': printf("\\r"); break;
            case '\t': printf("\\t"); break;
            case '\v': printf("\\v"); break;
            default: {
                if (isprint(c))
                    printf("%c",c);
                else
                    printf("\\%03d",c);
                break;
            }
        }
    }
    printf("\"");
}


static void printConstant(toku_State *T, int32_t t) {
    switch (t) {
        case TOKU_T_NIL:
            printf("nil");
            break;
        case TOKU_T_BOOL: {
            if (toku_to_bool(T, -1))
                printf("true");
            else
                printf("false");
            break;
        }
        case TOKU_T_NUMBER: {
            if (toku_is_integer(T, -1))
                printf(TOKU_INTEGER_FMT, toku_to_integer(T, -1));
            else {
                char buff[TOKU_N2SBUFFSZ];
                toku_numbertocstring(T, -1, buff);
                printf("%s", buff);
            }
            break;
        }
        case TOKU_T_STRING: {
            size_t l;
            const char *s = toku_to_lstring(T, -1, &l);
            printString(s, l);
            break;
        }
        default: toku_assert(0); /* unreachable */
    }
}


static void list_debug(toku_State *T, toku_Cinfo *ci, int32_t verb) {
    int32_t n = ci->nconstants;
    printf("constants (%d) for %p",n, ci->func);
    if (ci->constants)
        printf(" (at %p):", ci->constants);
    printf("\n");
    for (int32_t i=0; i<n; i++) {
        int32_t t = toku_getconstant(T, ci, i);
        printf("\t%d\t", i);
        printType(T, t);
        if (verb >= 3) {
            printf("\t");
            printConstant(T, t);
        }
        printf("\n");
        toku_pop(T, 1);
    }
    n = ci->nlocals;
    printf("locals (%d) for %p:\n", n, ci->func);
    for (int32_t i=0; i<n; i++) {
        const char *name = toku_getlocalinfo(T, ci, i);
        printf("\t%d\t%s\t%d\t%d\n",
                i, name, ci->info.l.startoff, ci->info.l.endoff);
    }
    n = ci->nupvals;
    printf("upvalues (%d) for %p:\n", n, ci->func);
    for (int32_t i=0; i<n; i++) {
        const char *name = toku_getupvalueinfo(T, ci, i);
        printf("\t%d\t%s\t%d\t%d\n",
                i, name, ci->info.u.instack, ci->info.u.idx);
    }
}


static void list_function(toku_State *T, toku_Cinfo *ci, int32_t verb) {
    toku_Cinfo dest;
    list_header(ci);
    list_opcode(T, ci, verb);
    if (verb >= 2) /* list debug information? */
        list_debug(T, ci, verb);
    for (int32_t i=0; toku_getfunction(T, ci, &dest, i); i++)
        list_function(T, &dest, verb);
}


static void list_opcodes(toku_State *T, int32_t verb) {
    toku_Cinfo ci;
    toku_getcompinfo(T, -1, &ci);
    list_function(T, &ci, verb);
}


static int32_t writer(toku_State *T, const void *b, size_t sz, void *data) {
    UNUSED(T);
    return (b==NULL || fwrite(b, sz, 1, cast(FILE*, data)) != 1) && (sz != 0);
}


#define errorfile(what) \
        tokuL_error(T, "cannot %s %s: %s", what, output, strerror(errno))


static int32_t pmain(toku_State *T) {
    int32_t argc = cast_i32(toku_to_integer(T, 0));
    char** argv = cast(char **, toku_to_userdata(T, 1));
    int32_t firstfunc;
    if (!toku_checkstack(T, argc + 1)) /* +1 for potential constant value */
        tokuL_error(T, "too many input files");
    firstfunc = toku_getntop(T);
    for (int32_t i=0; i<argc; i++) { /* load all input files */
        const char* filename = EQ(argv[i], "-") ? NULL : argv[i];
        if (tokuL_loadfile(T, filename) != TOKU_STATUS_OK)
            toku_error(T);
    }
    if (argc > 1) { /* more than one file loaded? */
        toku_combine(T, "=("TOKU_PROGNAMEC")", argc); /* combine them */
        toku_replace(T, firstfunc);
        toku_pop(T, argc - 1); /* leave only combination */
    }
    if (list) { ++list; list_opcodes(T, verbosity); }
    if (dump) {
        FILE *fp;
        fp = (output == NULL) ? stdout : fopen(output, "wb");
        if (fp == NULL) errorfile("open");
        toku_lock(T);
        toku_dump(T, writer, cast_voidp(fp), strip);
        toku_unlock(T);
        if (ferror(fp)) errorfile("write");
        if (fclose(fp)) errorfile("close");
        toku_pop(T, 1); /* remove dumped function */
        if (list == 1 && output != NULL) { /* didn't list anything yet? */
            if (tokuL_loadfile(T, output) != TOKU_STATUS_OK)
                toku_error(T); /* error message is on stack top */
            list_opcodes(T, verbosity);
            toku_pop(T, 1);
        }
    }
    return 0;
}


int32_t main(int32_t argc, char *argv[]) {
    int32_t i = cliargs(argc, argv);
    toku_State *T;
    argc -= i; argv += i;
    if (argc <= 0)
        ufatal("no input files given");
    T = tokuL_newstate();
    if (T == NULL)
        fatal("cannot create state: not enough memory");
    toku_push_cfunction(T, &pmain);
    toku_push_integer(T, argc);
    toku_push_lightuserdata(T, argv);
    if (toku_pcall(T, 2, 0, -1) != TOKU_STATUS_OK)
        fatal(toku_to_string(T, -1));
    toku_close(T);
    return EXIT_SUCCESS;
}
