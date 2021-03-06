/*
 * Copyright(c) 2020 ADLINK Technology Limited and others
 *
 * This program and the accompanying materials are made available under the
 * terms of the Eclipse Public License v. 2.0 which is available at
 * http://www.eclipse.org/legal/epl-2.0, or the Eclipse Distribution License
 * v. 1.0 which is available at
 * http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * SPDX-License-Identifier: EPL-2.0 OR BSD-3-Clause
 */
#include <assert.h>
#include <string.h>

#include "idl/version.h"
#include "idl/retcode.h"
#include "idl/processor.h"
#include "idl/string.h"

#include "idlcxx/backendCpp11Type.h"
#include "idlcxx/backendCpp11Trait.h"
#include "idlcxx/streamer_generator.h"

#ifdef _WIN32
#pragma warning(disable : 4996)
#endif

static char *
figure_guard(const char *file)
{
  char *inc = NULL;

  if (idl_asprintf(&inc, "DDSCXX_%s", file) == -1)
    return NULL;

  /* replace any non-alphanumeric characters */
  for (char *ptr = inc; *ptr; ptr++) {
    if ((*ptr >= 'a' && *ptr <= 'z'))
      *ptr = (char)('A' + (*ptr - 'a'));
    else if (!(*ptr >= 'A' && *ptr <= 'Z') && !(*ptr >= '0' && *ptr <= '9'))
      *ptr = '_';
  }

  return inc;
}

static idl_retcode_t
print_header(FILE *fh, const char *idl, const char *file)
{
  static const char fmt[] =
    "/****************************************************************\n"
    "\n"
    "  Generated by Eclipse Cyclone DDS IDL to CXX Translator\n"
    "  File name: %s\n"
    "  Source: %s\n"
    "  Cyclone DDS: v%s\n"
    "\n"
    "*****************************************************************/\n";

  fprintf(fh, fmt, file, idl, IDL_VERSION);
  return IDL_RETCODE_OK;
}

static idl_retcode_t
print_guard(FILE* fh, const char* inc)
{
  fprintf(fh, "#ifndef %s\n", inc);
  fprintf(fh, "#define %s\n\n", inc);
  return IDL_RETCODE_OK;
}

static idl_retcode_t
print_footer(FILE *fh, const char *inc)
{
  fprintf(fh, "#endif /* %s */\n", inc);
  return IDL_RETCODE_OK;
}

static idl_retcode_t
generate_types(
  const idl_tree_t *tree,
  const char *idl,
  const char *dir,
  const char *basename)
{
  FILE *fh;
  char *inc = NULL, *file = NULL;
  const char *sep = strlen(dir) ? "/" : "";
  idl_backend_ctx ctx = NULL;
  idl_retcode_t ret = IDL_RETCODE_OK;
  idl_ostream_t *stm;
  size_t len;

  if (idl_asprintf(&file, "%s%s%s.hpp", dir, sep, basename) == -1) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err;
  } else if (!(inc = figure_guard(file))) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_guard;
  } else if (!(ctx = idl_backend_context_new(2, tree->files->name, NULL))) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_context_new;
  } else if (!(fh = fopen(file, "w"))) {
    goto err_fopen;
  }

  ret = idl_backendGenerateType(ctx, tree);
  if (ret == IDL_RETCODE_OK)
  {
    ret = idl_backendGenerateTrait(ctx, tree);
    if (ret == IDL_RETCODE_OK)
    {
      print_header(fh, idl, file);
      print_guard(fh, inc);

      stm = idl_get_output_stream(ctx);
      assert(stm);
      len = get_ostream_buffer_position(stm);
      if (fwrite(get_ostream_buffer(stm), 1, len, fh) != len && ferror(fh)) {
        ret = IDL_RETCODE_CANNOT_OPEN_FILE;
      }
    }
  }
  fclose(fh);
err_fopen:
  idl_backend_context_free(ctx);
err_context_new:
  free(inc);
err_guard:
  free(file);
err:
  return ret;
}

static idl_retcode_t
generate_streamers(
  const idl_tree_t *tree,
  const char *idl,
  const char *dir,
  const char *basename)
{
  FILE *srcfh = NULL, *hdrfh = NULL;
  char *src = NULL, *hdr = NULL, *inc = NULL;
  const char *sep = strlen(dir) ? "/" : "";
  idl_streamer_output_t *generated = NULL;
  idl_retcode_t ret = IDL_RETCODE_OK;
  idl_ostream_t *stm = NULL;
  size_t len = 0;

  if (idl_asprintf(&src, "%s%s%s.cpp", dir, sep, basename) == -1) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_src;
  } else if (idl_asprintf(&hdr, "%s%s%s.hpp", dir, sep, basename) == -1) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_hdr;
  } else if (!(inc = figure_guard(hdr))) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_guard;
  } else if (!(srcfh = fopen(src, "wb"))) {
    ret = IDL_RETCODE_CANNOT_OPEN_FILE;
    goto err_src_fopen;
  } else if (!(hdrfh = fopen(hdr, "ab"))) {
    ret = IDL_RETCODE_CANNOT_OPEN_FILE;
    goto err_hdr_fopen;
  } else if (!(generated = create_idl_streamer_output())) {
    ret = IDL_RETCODE_NO_MEMORY;
    goto err_create_streamer_output;
  }

  idl_streamers_generate(tree, generated);
  stm = get_idl_streamer_impl_buf(generated);
  assert(stm);
  print_header(srcfh, idl, src);
  fprintf(srcfh, "\n#include \"%s\"\n", hdr);
  len = get_ostream_buffer_position(stm);
  if (fwrite(get_ostream_buffer(stm), 1, len, srcfh) != len && ferror(srcfh)) {
    ret = IDL_RETCODE_CANNOT_OPEN_FILE;
    goto err_write_impl_buf;
  }

  stm = get_idl_streamer_head_buf(generated);
  assert(stm);
  len = get_ostream_buffer_position(stm);
  if (fwrite(get_ostream_buffer(stm), 1, len, hdrfh) != len && ferror(hdrfh)) {
    ret = IDL_RETCODE_CANNOT_OPEN_FILE;
    goto err_write_head_buf;
  }

  print_footer(hdrfh, inc);


err_write_head_buf:
err_write_impl_buf:
  destruct_idl_streamer_output(generated);
err_create_streamer_output:
  fclose(hdrfh);
err_hdr_fopen:
  fclose(srcfh);
err_src_fopen:
  free(inc);
err_guard:
  free(hdr);
err_hdr:
  free(src);
err_src:
  return ret;
}

#if _WIN32
__declspec(dllexport)
#endif
idl_retcode_t
generate(const idl_tree_t *tree, const char *path)
{
  bool abs = false;
  const char *sep, *ext, *file;
  char *dir = NULL, *basename = NULL;

  if (!tree || !path)
    return IDL_RETCODE_SEMANTIC_ERROR; // not really, actually BAD_PARAMETER;

#if _WIN32
  if (((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z')) &&
       (path[1] == ':') &&
       (path[2] == '/' || path[2] == '\\'))
  {
    abs = true;
  }
#endif
  if (path[0] == '/') {
    abs = true;
  }

  /* use relative directory if user provided a relative path, use current
     work directory otherwise */
  sep = ext = NULL;
  for (const char *ptr = path; ptr[0]; ptr++) {
    if ((ptr[0] == '/' || ptr[0] == '\\') && ptr[1] != '\0')
      sep = ptr;
  }

  for (const char *ptr = (sep ? sep : path); ptr[0]; ptr++) {
    if (ptr[0] == '.')
      ext = ptr;
  }

  file = sep ? sep + 1 : path;
  dir = (!abs && sep) ? idl_strndup(path, (size_t)(sep-path)) : idl_strdup("");
  if (!dir) {
    return IDL_RETCODE_NO_MEMORY;
  } else if (!(basename = idl_strndup(file, ext ? (size_t)(ext-file) : strlen(file)))) {
    free(dir);
    return IDL_RETCODE_NO_MEMORY;
  }

  /* replace backslashes by forward slashes */
  for (char *ptr = dir; *ptr; ptr++) {
    if (*ptr == '\\')
      *ptr = '/';
  }

  generate_types(tree, path, dir, basename);
  generate_streamers(tree, path, dir, basename);

  free(dir);
  free(basename);
  return IDL_RETCODE_OK;
}
