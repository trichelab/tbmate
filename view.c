/* View .tbi with .tbk
 * 
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Wanding.Zhou@pennmedicine.upenn.edu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
**/

#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include "tbmate.h"
#include "wzmisc.h"
#include "wzio.h"
#include "wzbed.h"
#include "htslib/htslib/tbx.h"
#include "htslib/htslib/hts.h"
#include "htslib/htslib/regidx.h"
#include "htslib/htslib/kstring.h"

static int usage() {
  fprintf(stderr, "\n");
  fprintf(stderr, "Usage: tbmate view [options] <in.bed4i.gz> <out.tbk> [REGION [...]]\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "    -o        optional file output\n");
  fprintf(stderr, "    -g        REGION\n");
  fprintf(stderr, "    -R        file listing the regions\n");
  fprintf(stderr, "    -h        This help\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Note, in.bed4i.gz is a tabix-ed bed file. Column 4 is the .tbk offset.\n");
  fprintf(stderr, "\n");

  return 1;
}

static void error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  exit(EXIT_FAILURE);
}

static char **parse_regions(char *regions_fname, char *region, int *nregs) {
  kstring_t str = {0,0,0};
  int iseq = 0, ireg = 0;
  char **regs = NULL;
  *nregs = 0;

  if (regions_fname) {
    // improve me: this is a too heavy machinery for parsing regions...
    regidx_t *idx = regidx_init(regions_fname, NULL, NULL, 0, NULL);
    if (!idx) error("Could not build region list for \"%s\"\n", regions_fname);

    (*nregs) += regidx_nregs(idx);
    regs = (char**) malloc(sizeof(char*)*(*nregs));

    int nseq;
    char **seqs = regidx_seq_names(idx, &nseq);
    for (iseq=0; iseq<nseq; iseq++)
    {
      regitr_t itr;
      regidx_overlap(idx, seqs[iseq], 0, UINT32_MAX, &itr);
      while ( itr.i < itr.n )
      {
        str.l = 0;
        ksprintf(&str, "%s:%d-%d", seqs[iseq], REGITR_START(itr)+1, REGITR_END(itr)+1);
        regs[ireg++] = strdup(str.s);
        itr.i++;
      }
    }
    regidx_destroy(idx);
  }
  free(str.s);

  if (region) {

    char **fields; int nfields;
    line_get_fields(region, ",", &fields, &nfields);
    regs = realloc(regs, sizeof(char*)*((*nregs)+nfields));
    int i;
    for (i=0; i<nfields; ++i) {
      regs[(*nregs)+i] = fields[i];
    }
    (*nregs) += nfields;
    free(fields);
  }

  /* whole file */
  if (!*nregs) {
    regs = (char**) malloc(sizeof(char*));
    if (!regs) error(NULL);
    regs[0] = strdup(".");
    if (!regs[0]) error(NULL);
    *nregs = 1;
  }
  
  return regs;
}

typedef struct tbk_t {
  char *fname;
  FILE *fh;
  int offset;   /* offset in the number of units or byte if unit is sub-byte */
  data_type_t dt;               /* data type */
  uint8_t data;                 /* sub-byte data */
} tbk_t;

void tbk_open(tbk_t *tbk) {
  tbk->fh = fopen(tbk->fname, "rb");
  fread(&tbk->dt, 4, 1, tbk->fh);
  /* fprintf(stderr, "%d", tbk->dt); */
  tbk->offset = 0;
}

void tbk_query(tbk_t *tbk, int offset, FILE *out_fh) {
  
  switch(tbk->dt) {
  case DT_INT2: {
    if(tbk->offset == 0 || offset/4 != tbk->offset-1) { /* need to read? */
      if(offset/4 != tbk->offset) {                       /* need to seek? */
        tbk->offset = offset/4;
        if(fseek(tbk->fh, offset/4+TBK_HEADER_SIZE, SEEK_SET))
          wzfatal("File %s cannot be seeked.\n", tbk->fname);
      }
      fread(&(tbk->data), 1, 1, tbk->fh); tbk->offset++;
    }
    fprintf(out_fh, "\t%d", ((tbk->data)>>((offset%4)*2)) & 0x3);
    break;
  }
  case DT_INT32: {
    if(offset != tbk->offset) {
      if(fseek(tbk->fh, offset*4+TBK_HEADER_SIZE, SEEK_SET))
        wzfatal("File %s cannot be seeked.\n", tbk->fname);
      tbk->offset = offset;
    }
    
    int data;
    fread(&data, 4, 1, tbk->fh); tbk->offset++;
    fprintf(out_fh, "\t%d", data);
    break;
  }
  case DT_FLOAT: {
    if(offset != tbk->offset) {
      if(fseek(tbk->fh, offset*4+TBK_HEADER_SIZE, SEEK_SET))
        wzfatal("File %s cannot be seeked.\n", tbk->fname);
      tbk->offset = offset;
    }
    
    float data;
    fread(&data, 4, 1, tbk->fh); tbk->offset++;
    fprintf(out_fh, "\t%f", data);
    break;
  }
  case DT_ONES: {
    if(offset != tbk->offset) {
      if(fseek(tbk->fh, offset*2+TBK_HEADER_SIZE, SEEK_SET))
        wzfatal("File %s cannot be seeked.\n", tbk->fname);
      tbk->offset = offset;
    }
    
    uint16_t data;
    fread(&data, 2, 1, tbk->fh); tbk->offset++;
    fprintf(out_fh, "\t%f", uint16_to_float(data));
    /* fprintf(out_fh, "\t%d", data); */
    break;
  }
  case DT_DOUBLE: {
    if(offset != tbk->offset) {
      if(fseek(tbk->fh, offset*8+TBK_HEADER_SIZE, SEEK_SET))
        wzfatal("File %s cannot be seeked.\n", tbk->fname);
      tbk->offset = offset;
    }
    
    float data;
    fread(&data, 8, 1, tbk->fh); tbk->offset++;
    fprintf(out_fh, "\t%f", data);
    break;
  }
  default: wzfatal("Unrecognized data type: %d.\n", tbk->dt);
  }
}

void tbk_close(tbk_t *tbk) {
  fclose(tbk->fh);
  memset(tbk, 0, sizeof(tbk_t));
}

static int query_regions(char *fname, char **regs, int nregs, tbk_t *tbks, int n_tbks, FILE *out_fh) {
  int i;
  htsFile *fp = hts_open(fname,"r");
  if(!fp) error("Could not read %s\n", fname);

  int k;
  for(k=0; k<n_tbks; ++k) tbk_open(&tbks[k]);

  tbx_t *tbx = tbx_index_load(fname);
  if(!tbx) error("Could not load .tbi/.csi index of %s\n", fname);
  kstring_t str = {0,0,0};
  const char **seq = NULL;
  char **fields; int nfields;
  int offset;
  for(i=0; i<nregs; i++) {
    hts_itr_t *itr = tbx_itr_querys(tbx, regs[i]);
    if(!itr) continue;
    while (tbx_itr_next(fp, tbx, itr, &str) >= 0) {

      line_get_fields(str.s, "\t", &fields, &nfields);
      if (nfields < 3)
        wzfatal("[%s:%d] Bed file has fewer than 3 columns.\n", __func__, __LINE__);
      ensure_number(fields[3]);
      offset = atoi(fields[3]);

      fputs(fields[0], out_fh);
      fputc('\t', out_fh);
      fputs(fields[1], out_fh);
      fputc('\t', out_fh);
      fputs(fields[2], out_fh);

      for(k=0; k<n_tbks; ++k) tbk_query(&tbks[k], offset, out_fh);
      fputc('\n', out_fh);
      free_fields(fields, nfields);
    }
    tbx_itr_destroy(itr);
  }
  free(seq);
  free(str.s);
  tbx_destroy(tbx);

  if(hts_close(fp)) error("hts_close returned non-zero status: %s\n", fname);

  for(k=0; k<n_tbks; ++k) tbk_close(&tbks[k]);

  for(i=0; i<nregs; i++) free(regs[i]);
  free(regs);
  return 0;
}

int main_view(int argc, char *argv[]) {

  int c;
  if (argc<2) return usage();

  char *regions_fname = NULL;
  char *region = NULL;
  FILE *out_fh = stdout;
  while ((c = getopt(argc, argv, "o:R:g:h"))>=0) {
    switch (c) {
    case 'o': out_fh = fopen(optarg, "w"); break;
    case 'R': regions_fname = optarg; break;
    case 'g': region = strdup(optarg); break;
    case 'h': return usage(); break;
    default: usage(); wzfatal("Unrecognized option: %c.\n", c);
    }
  }

  if (optind + 1 > argc) { 
    usage(); 
    wzfatal("Please supply index file and tbk file.\n"); 
  }

  int nregs = 0;
  char **regs = NULL;

  char *bed4i_fname = argv[optind++];
  int n_tbks = argc - optind;
  tbk_t *tbks = calloc(n_tbks, sizeof(tbk_t));
  int i;
  for(i=0; optind < argc; optind++, i++) tbks[i].fname = argv[optind];
  regs = parse_regions(regions_fname, region, &nregs);
  int ret;
  ret = query_regions(bed4i_fname, regs, nregs, tbks, n_tbks, out_fh);
  free(tbks);
  return ret;
}
