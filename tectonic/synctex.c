/* Summary of original C code; see end of file for full comments:
 * Copyright (c) 2008, 2009, 2010, 2011 jerome DOT laurens AT u-bourgogne DOT fr
 * Latest Revision: Fri Apr 15 19:10:57 UTC 2011
 * MIT License.
 */

#include <tectonic/tectonic.h>
#include <tectonic/internals.h>
#include <tectonic/xetexd.h>
#include <tectonic/synctex.h>
#include <tectonic/core-bridge.h>

#include <stdio.h>
#include <stdarg.h>

#define SYNCTEX_VERSION 1

/* formerly synctex-xetex.h */

#define SYNCTEX_CURH (cur_h + 4736287)
#define SYNCTEX_CURV (cur_v + 4736287)
#define synchronization_field_size 1

/* in XeTeX, "halfword" fields are at least 32 bits, so we'll use those for
 * tag and line so that the sync field size is only one memory_word. */

#define SYNCTEX_TAG_MODEL(NODE,TYPE) zmem[NODE+TYPE##_node_size-synchronization_field_size].hh.v.LH
#define SYNCTEX_LINE_MODEL(NODE,TYPE) zmem[NODE+TYPE##_node_size-synchronization_field_size].hh.v.RH

/* end of synctex-xetex.h */

/* these values are constants in the WEB code: */
#define box_node_size (7 + synchronization_field_size) /* = 8 */
#define small_node_size 2
#define medium_node_size (small_node_size + synchronization_field_size) /* = 3 */
#define rule_node_size (4 + synchronization_field_size)
#define glue_node_size medium_node_size /* this comes into play via SYNCTEX_{TAG,LINE}_MODEL */
#define kern_node_size medium_node_size
#define math_node_size medium_node_size
#define width_offset 1
#define depth_offset 2
#define height_offset 3
#define rule_node 2
#define glue_node 10
#define kern_node 11

#define SYNCTEX_TYPE(NODE) zmem[NODE].hh.u.B0
#define SYNCTEX_SUBTYPE(NODE) zmem[NODE].hh.u.B1
#define SYNCTEX_WIDTH(NODE) zmem[NODE + width_offset].cint
#define SYNCTEX_DEPTH(NODE) zmem[NODE + depth_offset].cint
#define SYNCTEX_HEIGHT(NODE) zmem[NODE + height_offset].cint

/*  For non-GCC compilation.  */
#if !defined(__GNUC__) || (__GNUC__ < 2)
#    define __attribute__(A)
#endif

/*  UNIT is the scale. TeX coordinates are very accurate and client won't need
 *  that, at leat in a first step.  1.0 <-> 2^16 = 65536.
 *  The TeX unit is sp (scaled point) or pt/65536 which means that the scale
 *  factor to retrieve a bp unit (a postscript) is 72/72.27/65536 =
 *  1/4096/16.06 = 1/8192/8.03
 *  Here we use 1/SYNCTEX_UNIT_FACTOR as scale factor, then we can limit ourselves to
 *  integers. This default value assumes that TeX magnification factor is 1000.
 *  The real TeX magnification factor is used to fine tune the synctex context
 *  scale in the synctex_dot_open function.
 *  IMPORTANT: We can say that the natural unit of .synctex files is SYNCTEX_UNIT_FACTOR sp.
 *  To retrieve the proper bp unit, we'll have to divide by 8.03.  To reduce
 *  rounding errors, we'll certainly have to add 0.5 for non negative integers
 *  and +/-0.5 for negative integers.  This trick is mainly to gain speed and
 *  size. A binary file would be more appropriate in that respect, but I guess
 *  that some clients like auctex would not like it very much.  we cannot use
 *  "<<13" instead of "/SYNCTEX_UNIT_FACTOR" because the integers are signed and we do not
 *  want the sign bit to be propagated.  The origin of the coordinates is at
 *  the top left corner of the page.  For pdf mode, it is straightforward, but
 *  for dvi mode, we'll have to record the 1in offset in both directions,
 *  eventually modified by the magnification.
 */

typedef void (*synctex_recorder_t) (int32_t);  /* recorders know how to record a node */

#   define SYNCTEX_BITS_PER_BYTE 8

/*  Here are all the local variables gathered in one "synchronization context"  */
static struct {
    rust_output_handle_t file;  /*  the foo.synctex or foo.synctex.gz I/O identifier  */
    char *root_name;            /*  in general jobname.tex  */
    integer count;              /*  The number of interesting records in "foo.synctex"  */
    /*  next concern the last sync record encountered  */
    int32_t node;              /*  the last synchronized node, must be set
                                 *  before the recorder */
    synctex_recorder_t recorder;/*  the recorder of the node above, the
                                 *  routine that knows how to record the
                                 *  node to the .synctex file */
    integer tag, line;          /*  current tag and line  */
    integer curh, curv;         /*  current point  */
    integer magnification;      /*  The magnification as given by \mag */
    integer unit;               /*  The unit, defaults to 1, use 8192 to produce shorter but less accurate info */
    integer total_length;       /*  The total length of the bytes written since the last check point  */
    unsigned int synctex_tag_counter;   /* Global tag counter, used to be a local static in
                                         * synctex_start_input */
    struct _flags {
        unsigned int off:1;         /*  Definitely turn off synctex, corresponds to cli option -synctex=0 */
        unsigned int not_void:1;    /*  Whether it really contains synchronization material */
        unsigned int warn:1;        /*  One shot warning flag */
        unsigned int output_p:1;    /*  Whether the output_directory is used */
        unsigned int reserved:SYNCTEX_BITS_PER_BYTE*sizeof(int)-7; /* Align */
    } flags;
} synctex_ctxt = {
    NULL, NULL, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, {0,0,0,0,0}};


static char *
get_current_name (void)
{
    /* This used to always make the pathname absolute but I'm getting rid of
     * that since it ends up adding dependencies on a bunch of functions I
     * don't want to have to deal with. */

    if (!fullnameoffile)
        return xstrdup("");

    return xstrdup(fullnameoffile);
}


void
synctex_init_command(void)
{
    CACHE_THE_EQTB;

    /* In the web2c implementations this dealt with the -synctex command line
     * argument. */

    /* Reset state */
    synctex_ctxt.file = NULL;
    synctex_ctxt.root_name = NULL;
    synctex_ctxt.count = 0;
    synctex_ctxt.node = 0;
    synctex_ctxt.recorder = NULL;
    synctex_ctxt.tag = 0;
    synctex_ctxt.line = 0;
    synctex_ctxt.curh = 0;
    synctex_ctxt.curv = 0;
    synctex_ctxt.magnification = 0;
    synctex_ctxt.unit = 0;
    synctex_ctxt.total_length = 0;
    synctex_ctxt.synctex_tag_counter = 0;
    synctex_ctxt.flags.off = 0;
    synctex_ctxt.flags.not_void = 0;
    synctex_ctxt.flags.warn = 0;
    synctex_ctxt.flags.output_p = 0;

    INTPAR(synctex) = 0; /* \synctex=0 : don't record stuff */
    synctex_ctxt.flags.off = 0; /* we're not forcibly disabled though: user code can override */
}


/*  Free all memory used, close the file if any,
 *  It is sent locally when there is a problem with synctex output.
 *  It is sent by pdftex when a fatal error occurred in pdftex.web. */
static void
synctexabort(void)
{
    if (synctex_ctxt.file) {
        ttstub_output_close(synctex_ctxt.file);
        synctex_ctxt.file = NULL;
    }

    if (NULL != synctex_ctxt.root_name) {
        free(synctex_ctxt.root_name);
        synctex_ctxt.root_name = NULL;
    }

    synctex_ctxt.flags.off = 1;      /* disable synctex */
}

static inline int synctex_record_preamble(void);
static inline int synctex_record_input(integer tag, char *name);
static inline int synctex_record_postamble(void);
static inline int synctex_record_content(void);
static inline int synctex_record_settings(void);
static inline int synctex_record_sheet(integer sheet);
static inline int synctex_record_teehs(integer sheet);
static inline void synctex_record_vlist(int32_t p);
static inline void synctex_record_tsilv(int32_t p);
static inline void synctex_record_void_vlist(int32_t p);
static inline void synctex_record_hlist(int32_t p);
static inline void synctex_record_tsilh(int32_t p);
static inline void synctex_record_void_hlist(int32_t p);
static void synctex_math_recorder(int32_t p);
static inline void synctex_record_glue(int32_t p);
static inline void synctex_record_kern(int32_t p);
static inline void synctex_record_rule(int32_t p);
static void synctex_kern_recorder(int32_t p);
static void synctex_char_recorder(int32_t p);
static void synctex_node_recorder(int32_t p);

static const char *synctex_suffix = ".synctex";
static const char *synctex_suffix_gz = ".gz";


/*  synctex_dot_open ensures that the foo.synctex file is open.
 *  In case of problem, it definitely disables synchronization.
 *  Now all the output synchronization info is gathered in only one file.
 *  It is possible to split this info into as many different output files as sheets
 *  plus 1 for the control but the overall benefits are not so clear.
 *  For example foo-i.synctex would contain input synchronization
 *  information for page i alone.
 */
static rust_output_handle_t
synctex_dot_open(void)
{
    CACHE_THE_EQTB;
    char *tmp = NULL, *the_name = NULL;
    size_t len;

    if (synctex_ctxt.flags.off || !INTPAR(synctex))
        return NULL;

    if (synctex_ctxt.file)
        return synctex_ctxt.file;

    tmp = gettexstring(job_name);
    len = strlen(tmp);

    if (len <= 0)
        /*printf("\nSyncTeX information: no synchronization with keyboard input\n");*/
        goto fail;

    the_name = xmalloc(len
                            + strlen(synctex_suffix)
                            + strlen(synctex_suffix_gz)
                            + 1);
    strcpy(the_name, tmp);
    strcat(the_name, synctex_suffix);
    free(tmp);
    tmp = NULL;

    synctex_ctxt.file = ttstub_output_open(the_name, 0);
    if (synctex_ctxt.file == NULL)
        goto fail;

    if (synctex_record_preamble())
        /*printf("\nSyncTeX warning: no synchronization, problem with %s\n", the_name);*/
        goto fail;

    synctex_ctxt.magnification = 1000;
    synctex_ctxt.unit = 1;
    free(the_name);
    the_name = NULL;

    if (synctex_ctxt.root_name != NULL) {
        synctex_record_input(1, synctex_ctxt.root_name);
        free(synctex_ctxt.root_name);
        synctex_ctxt.root_name = NULL;
    }

    synctex_ctxt.count = 0;
    return synctex_ctxt.file;

fail:
    if (tmp)
        free(tmp);
    if (the_name)
        free(the_name);

    synctexabort();
    return NULL;
}

/*  Each time TeX opens a file, it sends a synctexstartinput message and enters
 *  this function.  Here, a new synchronization tag is created and stored in
 *  the synctex_tag of the TeX current input context.  Each synchronized
 *  TeX node will record this tag instead of the file name.  synctexstartinput
 *  writes the mapping synctag <-> file name to the .synctex (or .synctex.gz) file.  A client
 *  will read the .synctex file and retrieve this mapping, it will be able to
 *  open the correct file just knowing its tag.  If the same file is read
 *  multiple times, it might be associated to different tags.  Synchronization
 *  controllers, either in viewers, editors or standalone should be prepared to
 *  handle this situation and take the appropriate action if they want to
 *  optimize memory.  No two different files will have the same positive tag.
 *  It is not advisable to definitely store the file names here.  If the file
 *  names ever have to be stored, it should definitely be done at the TeX level
 *  just like src-specials do, such that other components of the program can use
 *  it.  This function does not make any difference between the files, it
 *  treats the same way .tex, .aux, .sty ... files, even if many of them do not
 *  contain any material meant to be typeset.
 */
void synctex_start_input(void)
{
    if (synctex_ctxt.flags.off) {
        return;
    }
    /*  synctex_tag_counter is a counter uniquely identifying the file actually
     *  open.  Each time tex opens a new file, synctexstartinput will increment this
     *  counter  */
    if (~synctex_ctxt.synctex_tag_counter > 0) {
        ++synctex_ctxt.synctex_tag_counter;
    } else {
        /*  we have reached the limit, subsequent files will be softly ignored
         *  this makes a lot of files... even in 32 bits
         *  Maybe we will limit this to 16bits and
         *  use the 16 other bits to store the column number */
        synctex_ctxt.synctex_tag_counter = 0;
        /* was this, but this looks like a bug */
        /* cur_input.synctex_tag = 0; */
        return;
    }
    cur_input.synctex_tag = (int) synctex_ctxt.synctex_tag_counter;     /*  -> *TeX.web  */
    if (synctex_ctxt.synctex_tag_counter == 1) {
        /*  this is the first file TeX ever opens, in general \jobname.tex we
         *  do not know yet if synchronization will ever be enabled so we have
         *  to store the file name, because we will need it later.
         *  This is necessary because \jobname can be different */
        synctex_ctxt.root_name = get_current_name();
        if (!strlen(synctex_ctxt.root_name)) {
            synctex_ctxt.root_name = xrealloc(synctex_ctxt.root_name, strlen("texput") + 1);
            strcpy(synctex_ctxt.root_name, "texput");
        }
        return;
    }
    if (synctex_ctxt.file
        || (0 != synctex_dot_open())) {
        char *tmp = get_current_name();
        /* Always record the input, even if INTPAR(synctex) is 0 */
        synctex_record_input(cur_input.synctex_tag, tmp);
        free(tmp);
    }
    return;
}

/*  All the synctex... functions below have the smallest set of parameters.  It
 *  appears to be either the address of a node, or nothing at all.  Using zmem,
 *  which is the place where all the nodes are stored, one can retrieve every
 *  information about a node.  The other information is obtained through the
 *  global context variable.
 */

/*  Free all memory used and close the file,
 *  sent by close_files_and_terminate in tex.web.
 *  synctexterminate() is called when the TeX run terminates.
 */
void synctex_terminate(boolean log_opened)
{
    if (synctex_ctxt.file) {
        /* We keep the file even if no tex output is produced
         * (synctex_ctxt.flags.not_void == 0). I assume that this means that there
         * was an error and tectonic will not save anything anyway. */
        synctex_record_postamble();
        ttstub_output_close(synctex_ctxt.file);
        synctex_ctxt.file = NULL;
    }
    synctexabort();
}

/*  Recording the "{..." line.  In *tex.web, use synctex_sheet(pdf_output) at
 *  the very beginning of the ship_out procedure.
 */
void synctex_sheet(integer mag)
{
    CACHE_THE_EQTB;

    if (synctex_ctxt.flags.off) {
        if (INTPAR(synctex) && !synctex_ctxt.flags.warn) {
            synctex_ctxt.flags.warn = 1;
            ttstub_issue_warning("SyncTeX was disabled from the command line with --synctex=0\nChanging the value of \\synctex has no effect.");
        }
        return;
    }
    if (synctex_ctxt.file
        || (INTPAR(synctex) && (0 != synctex_dot_open()))) {
        /*  First possibility: the .synctex file is already open because SyncTeX was activated on the CLI
         *  or it was activated with the \synctex macro and the first page is already shipped out.
         *  Second possibility: tries to open the .synctex, useful if synchronization was enabled
         *  from the source file and not from the CLI. */
        if (total_pages == 0) {
            /*  Now it is time to properly set up the scale factor. */
            if (mag > 0) {
                synctex_ctxt.magnification = mag;
            }
            if (0 != synctex_record_settings() || 0 != synctex_record_content()) {
                synctexabort();
                return;
            }
        }
        synctex_record_sheet(total_pages + 1);
    }
    return;
}

/*  Recording the "}..." line.  In *tex.web, use synctex_teehs at
 *  the very end of the ship_out procedure.
 */
void synctex_teehs(void)
{
    if (synctex_ctxt.flags.off || !synctex_ctxt.file) {
        return;
    }
    synctex_record_teehs(total_pages);/* not total_pages+1*/
    return;
}


/*  When an hlist ships out, it can contain many different kern/glue nodes with
 *  exactly the same sync tag and line.  To reduce the size of the .synctex
 *  file, we only display a kern node sync info when either the sync tag or the
 *  line changes.  Also, we try ro reduce the distance between the chosen nodes
 *  in order to improve accuracy.  It means that we display information for
 *  consecutive nodes, as far as possible.  This tricky part uses a "recorder",
 *  which is the address of the routine that knows how to write the
 *  synchronization info to the .synctex file.  It also uses criteria to detect
 *  a change in the context, this is the macro SYNCTEX_???_CONTEXT_DID_CHANGE. The
 *  SYNCTEX_IGNORE macro is used to detect unproperly initialized nodes.  See
 *  details in the implementation of the functions below.  */
#   define SYNCTEX_IGNORE(NODE) synctex_ctxt.flags.off || !INTPAR(synctex) || !synctex_ctxt.file


/*  This message is sent when a vlist will be shipped out, more precisely at
 *  the beginning of the vlist_out procedure in *TeX.web.  It will be balanced
 *  by a synctex_tsilv, sent at the end of the vlist_out procedure.  p is the
 *  address of the vlist We assume that p is really a vlist node! */
void synctex_vlist(int32_t this_box)
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(this_box)) {
        return;
    }
    synctex_ctxt.node = this_box;   /*  0 to reset  */
    synctex_ctxt.recorder = NULL;   /*  reset  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(this_box,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(this_box,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_record_vlist(this_box);
}


/*  Recording a "f" line ending a vbox: this message is sent whenever a vlist
 *  has been shipped out. It is used to close the vlist nesting level. It is
 *  sent at the end of the vlist_out procedure in *TeX.web to balance a former
 *  synctex_vlist sent at the beginning of that procedure.    */
void synctex_tsilv(int32_t this_box)
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(this_box)) {
        return;
    }
    /*  Ignoring any pending info to be recorded  */
    synctex_ctxt.node = this_box; /*  0 to reset  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(this_box,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(this_box,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;
    synctex_record_tsilv(this_box);
}


/*  This message is sent when a void vlist will be shipped out.
 *  There is no need to balance a void vlist.  */
void synctex_void_vlist(int32_t p, int32_t this_box __attribute__ ((unused)))
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(p)) {
        return;
    }
    synctex_ctxt.node = p;          /*  reset  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;   /*  reset  */
    synctex_record_void_vlist(p);
}


/*  This message is sent when an hlist will be shipped out, more precisely at
 *  the beginning of the hlist_out procedure in *TeX.web.  It will be balanced
 *  by a synctex_tsilh, sent at the end of the hlist_out procedure.  p is the
 *  address of the hlist We assume that p is really an hlist node! */
void synctex_hlist(int32_t this_box)
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(this_box)) {
        return;
    }
    synctex_ctxt.node = this_box;   /*  0 to reset  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(this_box,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(this_box,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;   /*  reset  */
    synctex_record_hlist(this_box);
}


/*  Recording a ")" line ending an hbox this message is sent whenever an hlist
 *  has been shipped out it is used to close the hlist nesting level. It is
 *  sent at the end of the hlist_out procedure in *TeX.web to balance a former
 *  synctex_hlist sent at the beginning of that procedure.    */
void synctex_tsilh(int32_t this_box)
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(this_box)) {
        return;
    }
    /*  Ignoring any pending info to be recorded  */
    synctex_ctxt.node = this_box;     /*  0 to force next node to be recorded!  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(this_box,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(this_box,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;   /*  reset  */
    synctex_record_tsilh(this_box);
}


/*  This message is sent when a void hlist will be shipped out.
 *  There is no need to balance a void hlist.  */
void synctex_void_hlist(int32_t p, int32_t this_box __attribute__ ((unused)))
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(p)) {
        return;
    }
    /*  the sync context has changed  */
    if (synctex_ctxt.recorder != NULL) {
        /*  but was not yet recorded  */
        (*synctex_ctxt.recorder) (synctex_ctxt.node);
    }
    synctex_ctxt.node = p;          /*  0 to reset  */
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,box);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,box);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;   /*  reset  */
    synctex_record_void_hlist(p);
}

/*  This macro will detect a change in the synchronization context.  As long as
 *  the synchronization context remains the same, there is no need to write
 *  synchronization info: it would not help more.  The synchronization context
 *  has changed when either the line number or the file tag has changed.  */
#   define SYNCTEX_CONTEXT_DID_CHANGE(NODE,TYPE) ((0 == synctex_ctxt.node)\
|| (SYNCTEX_TAG_MODEL(NODE,TYPE) != synctex_ctxt.tag)\
|| (SYNCTEX_LINE_MODEL(NODE,TYPE) != synctex_ctxt.line))


/*  glue code, this message is sent whenever an inline math node will ship out
 See: @ @<Output the non-|char_node| |p| for...  */
void synctex_math(int32_t p, int32_t this_box __attribute__ ((unused)))
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(p)) {
        return;
    }
    if ((synctex_ctxt.recorder != NULL) && SYNCTEX_CONTEXT_DID_CHANGE(p,math)) {
        /*  the sync context did change  */
        (*synctex_ctxt.recorder) (synctex_ctxt.node);
    }
    synctex_ctxt.node = p;
    synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,math);
    synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,math);
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;/*  no need to record once more  */
    synctex_math_recorder(p);/*  always record synchronously  */
}

/*  this message is sent whenever an horizontal glue node or rule node ships out
 See: move_past:...    */
#   undef SYNCTEX_IGNORE
#   define SYNCTEX_IGNORE(NODE,TYPE) synctex_ctxt.flags.off || !INTPAR(synctex) \
|| (0 >= SYNCTEX_TAG_MODEL(NODE,TYPE)) \
|| (0 >= SYNCTEX_LINE_MODEL(NODE,TYPE))
void synctex_horizontal_rule_or_glue(int32_t p, int32_t this_box __attribute__ ((unused)))
{
    CACHE_THE_EQTB;

    switch (SYNCTEX_TYPE(p)) {
        case rule_node:
            if (SYNCTEX_IGNORE(p,rule)) {
                return;
            }
            break;
        case glue_node:
            if (SYNCTEX_IGNORE(p,glue)) {
                return;
            }
            break;
        case kern_node:
            if (SYNCTEX_IGNORE(p,kern)) {
                return;
            }
            break;
        default:
            ttstub_issue_error("unknown node type %d in SyncTeX", SYNCTEX_TYPE(p));
    }
    synctex_ctxt.node = p;
    synctex_ctxt.curh = SYNCTEX_CURH;
    synctex_ctxt.curv = SYNCTEX_CURV;
    synctex_ctxt.recorder = NULL;
    switch (SYNCTEX_TYPE(p)) {
        case rule_node:
            synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,rule);
            synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,rule);
            synctex_record_rule(p); /*  always record synchronously: maybe some text is outside the box  */
            break;
        case glue_node:
            synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,glue);
            synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,glue);
            synctex_record_glue(p); /*  always record synchronously: maybe some text is outside the box  */
            break;
        case kern_node:
            synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,kern);
            synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,kern);
            synctex_record_kern(p); /*  always record synchronously: maybe some text is outside the box  */
            break;
        default:
            ttstub_issue_error("unknown node type %d in SyncTeX", SYNCTEX_TYPE(p));
    }
}


/*  this message is sent whenever a kern node ships out
 See: @ @<Output the non-|char_node| |p| for...    */
void synctex_kern(int32_t p, int32_t this_box)
{
    CACHE_THE_EQTB;

    if (SYNCTEX_IGNORE(p,kern)) {
        return;
    }
    if (SYNCTEX_CONTEXT_DID_CHANGE(p,kern)) {
        /*  the sync context has changed  */
        if (synctex_ctxt.recorder != NULL) {
            /*  but was not yet recorded  */
            (*synctex_ctxt.recorder) (synctex_ctxt.node);
        }
        if (synctex_ctxt.node == this_box) {
            /* first node in the list */
            synctex_ctxt.node = p;
            synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,kern);
            synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,kern);
            synctex_ctxt.recorder = &synctex_kern_recorder;
        } else {
            synctex_ctxt.node = p;
            synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,kern);
            synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,kern);
            synctex_ctxt.recorder = NULL;
            /*  always record when the context has just changed
             *  and when not the first node  */
            synctex_kern_recorder(p);
        }
    } else {
        /*  just update the geometry and type (for future improvements)  */
        synctex_ctxt.node = p;
        synctex_ctxt.tag = SYNCTEX_TAG_MODEL(p,kern);
        synctex_ctxt.line = SYNCTEX_LINE_MODEL(p,kern);
        synctex_ctxt.recorder = &synctex_kern_recorder;
    }
}


#   undef SYNCTEX_IGNORE
#   define SYNCTEX_IGNORE(NODE) (synctex_ctxt.flags.off || !INTPAR(synctex) || !synctex_ctxt.file)

/*  this message should be sent to record information
 synchronously for the current location    */
void
synctex_current(void)
{
    CACHE_THE_EQTB;
    int len;

    if (SYNCTEX_IGNORE(nothing))
        return;

    len = ttstub_fprintf(synctex_ctxt.file, "x%i,%i:%i,%i\n",
                  synctex_ctxt.tag,synctex_ctxt.line,
                  SYNCTEX_CURH / synctex_ctxt.unit,
                  SYNCTEX_CURV / synctex_ctxt.unit);

    if (len > 0)
        synctex_ctxt.total_length += len;
    else
        synctexabort();
}

static inline int
synctex_record_settings(void)
{
    CACHE_THE_EQTB;
    int len;

    if (NULL == synctex_ctxt.file)
        return 0;

    len = ttstub_fprintf(synctex_ctxt.file, "Output:pdf\nMagnification:%i\nUnit:%i\nX Offset:0\nY Offset:0\n",
                  synctex_ctxt.magnification,
                  synctex_ctxt.unit); /* magic pt/in conversion */

    if (len > 0) {
        synctex_ctxt.total_length += len;
        return 0;
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_preamble(void)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "SyncTeX Version:%i\n", SYNCTEX_VERSION);

    if (len > 0) {
        synctex_ctxt.total_length = len;
        return 0;
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_input(integer tag, char *name)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "Input:%i:%s\n", tag, name);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        return 0;
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_anchor(void)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "!%i\n", synctex_ctxt.total_length);

    if (len > 0) {
        synctex_ctxt.total_length = len;
        ++synctex_ctxt.count;
        return 0;
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_content(void)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "Content:\n");

    if (len > 0)
        synctex_ctxt.total_length += len;
    else
        synctexabort();
}

static inline int
synctex_record_sheet(integer sheet)
{
    if (0 == synctex_record_anchor()) {
        int len = ttstub_fprintf(synctex_ctxt.file, "{%i\n", sheet);
        if (len > 0) {
            synctex_ctxt.total_length += len;
            ++synctex_ctxt.count;
            return 0;
        }
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_teehs(integer sheet)
{
    if (0 == synctex_record_anchor()) {
        int len = ttstub_fprintf(synctex_ctxt.file, "}%i\n", sheet);
        if (len > 0) {
            synctex_ctxt.total_length += len;
            ++synctex_ctxt.count;
            return 0;
        }
    }

    synctexabort();
    return -1;
}

static inline void
synctex_record_void_vlist(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "v%i,%i:%i,%i:%i,%i,%i\n",
                      SYNCTEX_TAG_MODEL(p,box),
                      SYNCTEX_LINE_MODEL(p,box),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit,
                      SYNCTEX_WIDTH(p) / synctex_ctxt.unit,
                      SYNCTEX_HEIGHT(p) / synctex_ctxt.unit,
                      SYNCTEX_DEPTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_vlist(int32_t p)
{
    int len;

    synctex_ctxt.flags.not_void = 1;

    len = ttstub_fprintf(synctex_ctxt.file, "[%i,%i:%i,%i:%i,%i,%i\n",
                  SYNCTEX_TAG_MODEL(p,box),
                  SYNCTEX_LINE_MODEL(p,box),
                  synctex_ctxt.curh / synctex_ctxt.unit,
                  synctex_ctxt.curv / synctex_ctxt.unit,
                  SYNCTEX_WIDTH(p) / synctex_ctxt.unit,
                  SYNCTEX_HEIGHT(p) / synctex_ctxt.unit,
                  SYNCTEX_DEPTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_tsilv(int32_t p __attribute__ ((unused)))
{
    int len = ttstub_fprintf(synctex_ctxt.file, "]\n");

    if (len > 0) {
        synctex_ctxt.total_length += len;
        /* is it correct that synctex_ctxt.count is not incremented here? */
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_void_hlist(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "h%i,%i:%i,%i:%i,%i,%i\n",
                      SYNCTEX_TAG_MODEL(p,box),
                      SYNCTEX_LINE_MODEL(p,box),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit,
                      SYNCTEX_WIDTH(p) / synctex_ctxt.unit,
                      SYNCTEX_HEIGHT(p) / synctex_ctxt.unit,
                      SYNCTEX_DEPTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_hlist(int32_t p)
{
    int len;

    synctex_ctxt.flags.not_void = 1;

    len = ttstub_fprintf(synctex_ctxt.file, "(%i,%i:%i,%i:%i,%i,%i\n",
                  SYNCTEX_TAG_MODEL(p,box),
                  SYNCTEX_LINE_MODEL(p,box),
                  synctex_ctxt.curh / synctex_ctxt.unit,
                  synctex_ctxt.curv / synctex_ctxt.unit,
                  SYNCTEX_WIDTH(p) / synctex_ctxt.unit,
                  SYNCTEX_HEIGHT(p) / synctex_ctxt.unit,
                  SYNCTEX_DEPTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_tsilh(int32_t p __attribute__ ((unused)))
{
    int len = ttstub_fprintf(synctex_ctxt.file, ")\n");

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline int
synctex_record_count(void)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "Count:%i\n", synctex_ctxt.count);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        return 0;
    }

    synctexabort();
    return -1;
}

static inline int
synctex_record_postamble(void)
{
    if (0 == synctex_record_anchor()) {
        int len = ttstub_fprintf(synctex_ctxt.file, "Postamble:\n");
        if (len > 0) {
            synctex_ctxt.total_length += len;
            if (!synctex_record_count() && !synctex_record_anchor()) {
                len = ttstub_fprintf(synctex_ctxt.file, "Post scriptum:\n");
                if (len > 0) {
                    synctex_ctxt.total_length += len;
                    return 0;
                }
            }
        }
    }

    synctexabort();
    return -1;
}

static inline void
synctex_record_glue(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "g%i,%i:%i,%i\n",
                      SYNCTEX_TAG_MODEL(p,glue),
                      SYNCTEX_LINE_MODEL(p,glue),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_kern(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "k%i,%i:%i,%i:%i\n",
                      SYNCTEX_TAG_MODEL(p,glue),
                      SYNCTEX_LINE_MODEL(p,glue),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit,
                      SYNCTEX_WIDTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static inline void
synctex_record_rule(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "r%i,%i:%i,%i:%i,%i,%i\n",
                      SYNCTEX_TAG_MODEL(p,rule),
                      SYNCTEX_LINE_MODEL(p,rule),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit,
                      rule_wd / synctex_ctxt.unit,
                      rule_ht / synctex_ctxt.unit,
                      rule_dp / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static void
synctex_math_recorder(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "$%i,%i:%i,%i\n",
                      SYNCTEX_TAG_MODEL(p, math),
                      SYNCTEX_LINE_MODEL(p, math),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static void
synctex_kern_recorder(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "k%i,%i:%i,%i:%i\n",
                      SYNCTEX_TAG_MODEL(p, kern),
                      SYNCTEX_LINE_MODEL(p, kern),
                      synctex_ctxt.curh / synctex_ctxt.unit,
                      synctex_ctxt.curv / synctex_ctxt.unit,
                      SYNCTEX_WIDTH(p) / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static void
synctex_char_recorder(int32_t p __attribute__ ((unused)))
{
    int len = ttstub_fprintf(synctex_ctxt.file, "c%i,%i\n",
                      synctex_ctxt.curh / synctex_ctxt.unit, synctex_ctxt.curv / synctex_ctxt.unit);

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}

static void
synctex_node_recorder(int32_t p)
{
    int len = ttstub_fprintf(synctex_ctxt.file, "?%i,%i:%i,%i\n",
                      synctex_ctxt.curh / synctex_ctxt.unit, synctex_ctxt.curv / synctex_ctxt.unit,
                      SYNCTEX_TYPE(p), SYNCTEX_SUBTYPE(p));

    if (len > 0) {
        synctex_ctxt.total_length += len;
        ++synctex_ctxt.count;
    } else {
        synctexabort();
    }
}


/*
 Copyright (c) 2008, 2009, 2010, 2011 jerome DOT laurens AT u-bourgogne DOT fr

 This file is part of the SyncTeX package.

 Latest Revision: Fri Apr 15 19:10:57 UTC 2011

 License:
 --------
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE

 Except as contained in this notice, the name of the copyright holder
 shall not be used in advertising or otherwise to promote the sale,
 use or other dealings in this Software without prior written
 authorization from the copyright holder.

 Important notice:
 -----------------
 This file is named "synctex.c", it may or may not have a header counterpart
 depending on its use.  It aims to provide basic components useful for the
 input/output synchronization technology for TeX.
 The purpose of the implementation is threefold
 - firstly, it defines a new input/output synchronization technology named
 "synchronize texnology", "SyncTeX" or "synctex"
 - secondly, it defines the naming convention and format of the auxiliary file
 used by this technology
 - thirdly, it defines the API of a controller and a controller, used in
 particular by the pdfTeX and XeTeX programs to prepare synchronization.

 All these are up to a great extent de facto definitions, which means that they
 are partly defined by the implementation itself.

 This technology was first designed for pdfTeX, an extension of TeX managing the
 pdf output file format, but it can certainly be adapted to other programs built
 from TeX as long as the extensions do not break too much the core design.
 Moreover, the synchronize texnology only relies on code concept and not
 implementation details, so it can be ported to other TeX systems.  In order to
 support SyncTeX, one can start reading the dedicated section in synctex.ch,
 sync-pdftex.ch and sync-xetex.ch. Actually, support is provided for TeX, e-TeX,
 pdfTeX and XeTeX.

 Other existing public synchronization technologies are defined by srcltx.sty -
 also used by source specials - and pdfsync.sty.  Like them, the synchronize
 texnology is meant to be shared by various text editors, viewers and TeX
 engines.  A centralized reference and source of information is available in TeX-Live.

 Versioning:
 -----------
 As synctex is embedded into different TeX implementation, there is an independent
 versionning system.
 For TeX implementations, the actual version is: 3
 For .synctex file format, the actual version is SYNCTEX_VERSION below

 Please, do not remove these explanations.

 Acknowledgments:
 ----------------
 The author received useful remarks from the pdfTeX developers, especially Hahn The Thanh,
 and significant help from XeTeX developer Jonathan Kew

 Nota Bene:
 ----------
 If you include or use a significant part of the synctex package into a software,
 I would appreciate to be listed as contributor and see "SyncTeX" highlighted.

 History:
 --------
 Version 1.14
 Fri Apr 15 19:10:57 UTC 2011
 - taking output_directory into account
 - Replaced FOPEN_WBIN_MODE by FOPEN_W_MODE when opening the text version of the .synctex file.
 - Merging with LuaTeX's version of synctex.c

 Version 3
 - very minor design change to take luatex into account
 - typo fixed
 - some size_t replaced by int
 - very minor code design change to remove wrong xetex specific warnings

 Version 2
 Fri Sep 19 14:55:31 UTC 2008
 - support for file names containing spaces.
 This is one thing that xetex and pdftex do not manage the same way.
 When the input file name contains a space character ' ',
 pdftex will automatically enclose this name between two quote characters '"',
 making programs believe that these quotes are really part of the name.
 xetex does nothing special.
 For that reason, running the command line
 xetex --synctex=-1 "my file.tex"
 is producing the expected file named <my file.synctex>, (the '<' and '>' are not part of the name)
 whereas running the command line
 pdftex --synctex=-1 "my file.tex"
 was producing the unexpected file named <"my file".synctex> where the two '"' chracters were part of the name.
 Of course, that was breaking the typesetting mechanism when pdftex was involved.
 To solve this problem, we prefer to rely on the output_file_name instead of the jobname.
 In the case when no output_file_name is available, we use jobname and test if the file name
 starts and ends with a quote character. Every synctex output file is removed because we consider
 TeX encontered a problem.
 There is some conditional coding.

 Version 1
 Latest Revision: Wed Jul  1 08:15:44 UTC 2009

 */
