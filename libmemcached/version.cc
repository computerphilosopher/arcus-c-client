/*
 * arcus-c-client : Arcus C client
 * Copyright 2010-2014 NAVER Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 * 
 *  Libmemcached library
 *
 *  Copyright (C) 2011 Data Differential, http://datadifferential.com/
 *  Copyright (C) 2010 Brian Aker All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *      * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *      * Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following disclaimer
 *  in the documentation and/or other materials provided with the
 *  distribution.
 *
 *      * The names of its contributors may not be used to endorse or
 *  promote products derived from this software without specific prior
 *  written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <libmemcached/common.h>
#include "libmemcached/arcus_priv.h"

const char * memcached_lib_version(void) 
{
  return LIBMEMCACHED_VERSION_STRING;
}

#if 1 // OPTIMIZE_MGET
static inline memcached_return_t memcached_version_binary_from_instance(memcached_server_write_instance_st instance);
static inline memcached_return_t memcached_version_textual_from_instance(memcached_server_write_instance_st instance);
#else
static inline memcached_return_t memcached_version_binary(memcached_st *ptr);
static inline memcached_return_t memcached_version_textual(memcached_st *ptr);
#endif

memcached_return_t memcached_version(memcached_st *ptr)
{
#if 1 // OPTIMIZE_MGET
  if (ptr->flags.use_udp || ptr->flags.no_reply)
#else
  if (ptr->flags.use_udp)
#endif
    return MEMCACHED_NOT_SUPPORTED;

  memcached_return_t rc;

  arcus_server_check_for_update(ptr);

#if 1 // OPTIMIZE_MGET
  rc= MEMCACHED_SUCCESS;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++)
  {
    memcached_return_t rrc;
    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);
    rrc= memcached_version_from_instance(instance);

    if (rrc != MEMCACHED_SUCCESS) {
      rc= MEMCACHED_SOME_ERRORS;
    }
  }
#else
  if (ptr->flags.binary_protocol)
    rc= memcached_version_binary(ptr);
  else
    rc= memcached_version_textual(ptr);
#endif

  return rc;
}

#if 1 // OPTIMIZE_MGET
memcached_return_t memcached_version_from_instance(memcached_server_write_instance_st instance)
{
  if (instance->root->flags.use_udp || instance->root->flags.no_reply)
    return MEMCACHED_NOT_SUPPORTED;

  memcached_return_t rc;

  if (instance->root->flags.binary_protocol)
    rc= memcached_version_binary_from_instance(instance);
  else
    rc= memcached_version_textual_from_instance(instance);

  if (instance->major_version != UINT8_MAX)
  {
    if (instance->is_enterprise)
    {
      if (instance->major_version > 0 || instance->minor_version > 6)
        instance->enable_optimized_mget= true;
    }
    else
    {
      if (instance->major_version > 1 || instance->minor_version > 10)
        instance->enable_optimized_mget= true;
    }
  }

  return rc;
}

static inline memcached_return_t memcached_version_textual_from_instance(memcached_server_write_instance_st instance)
{
  size_t send_length;
  char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];
  char *response_ptr;
  const char *command= "version\r\n";

  send_length= sizeof("version\r\n") -1;

  memcached_return_t rrc = MEMCACHED_SUCCESS;

  // Optimization, we only fetch version once.
  if (instance->major_version != UINT8_MAX)
    return rrc;

  rrc= memcached_do(instance, command, send_length, true);
  if (rrc != MEMCACHED_SUCCESS)
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SOME_ERRORS;
  }

  rrc= memcached_response(instance, buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, NULL);
  if (rrc != MEMCACHED_SUCCESS)
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SOME_ERRORS;
  }

  /* Find the space, and then move one past it to copy version */
  response_ptr= index(buffer, ' ');
  response_ptr++;

  // UNKNOWN
  if (*response_ptr == 'U')
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SUCCESS;
  }

  // response string format : VERSION x.x.x-E || VERSION x.x.x
  instance->is_enterprise = (strrchr(response_ptr, 'E') == NULL) ? false : true;

  instance->major_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
  if (errno == ERANGE)
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SOME_ERRORS;
  }

  response_ptr= index(response_ptr, '.');
  response_ptr++;

  instance->minor_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
  if (errno == ERANGE)
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SOME_ERRORS;
  }

  response_ptr= index(response_ptr, '.');
  response_ptr++;
  instance->micro_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
  if (errno == ERANGE)
  {
    instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
    return MEMCACHED_SOME_ERRORS;
  }

  return MEMCACHED_SUCCESS;
}

static inline memcached_return_t memcached_version_binary_from_instance(memcached_server_write_instance_st instance)
{
  protocol_binary_request_version request= {};
  request.message.header.request.magic= PROTOCOL_BINARY_REQ;
  request.message.header.request.opcode= PROTOCOL_BINARY_CMD_VERSION;
  request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;

  memcached_return_t rrc;

  if (instance->major_version != UINT8_MAX)
    return MEMCACHED_SUCCESS;

  rrc= memcached_do(instance, request.bytes, sizeof(request.bytes), true);
  if (rrc != MEMCACHED_SUCCESS)
  {
    memcached_io_reset(instance);
    return MEMCACHED_SOME_ERRORS;
  }

  if (memcached_server_response_count(instance) > 0)
  {
    char buffer[32];
    char *p;

    rrc= memcached_response(instance, buffer, sizeof(buffer), NULL);
    if (rrc != MEMCACHED_SUCCESS)
    {
      memcached_io_reset(instance);
      return MEMCACHED_SOME_ERRORS;
    }

    // response string format : VERSION x.x.x-E || VERSION x.x.x
    instance->is_enterprise = (strrchr(buffer+3, 'E') == NULL) ? false : true;

    instance->major_version= (uint8_t)strtol(buffer, &p, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      return MEMCACHED_SOME_ERRORS;
    }

    instance->minor_version= (uint8_t)strtol(p + 1, &p, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      return MEMCACHED_SOME_ERRORS;
    }

    instance->micro_version= (uint8_t)strtol(p + 1, NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      return MEMCACHED_SOME_ERRORS;
    }
  }

  return MEMCACHED_SUCCESS;
}
#endif

#if 1 // OPTIMIZE_MGET
#else
static inline memcached_return_t memcached_version_textual(memcached_st *ptr)
{
  size_t send_length;
  memcached_return_t rc;
  char buffer[MEMCACHED_DEFAULT_COMMAND_SIZE];
  char *response_ptr;
  const char *command= "version\r\n";

  send_length= sizeof("version\r\n") -1;

  rc= MEMCACHED_SUCCESS;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++)
  {
    memcached_return_t rrc;
    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    // Optimization, we only fetch version once.
    if (instance->major_version != UINT8_MAX)
      continue;

    rrc= memcached_do(instance, command, send_length, true);
    if (rrc != MEMCACHED_SUCCESS)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    rrc= memcached_response(instance, buffer, MEMCACHED_DEFAULT_COMMAND_SIZE, NULL);
    if (rrc != MEMCACHED_SUCCESS)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    /* Find the space, and then move one past it to copy version */
    response_ptr= index(buffer, ' ');
    response_ptr++;

    // UNKNOWN
    if (*response_ptr == 'U')
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      continue;
    }

    instance->major_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    response_ptr= index(response_ptr, '.');
    response_ptr++;

    instance->minor_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }

    response_ptr= index(response_ptr, '.');
    response_ptr++;
    instance->micro_version= (uint8_t)strtol(response_ptr, (char **)NULL, 10);
    if (errno == ERANGE)
    {
      instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }
  }

  return rc;
}

static inline memcached_return_t memcached_version_binary(memcached_st *ptr)
{
  memcached_return_t rc;
  protocol_binary_request_version request= {};
  request.message.header.request.magic= PROTOCOL_BINARY_REQ;
  request.message.header.request.opcode= PROTOCOL_BINARY_CMD_VERSION;
  request.message.header.request.datatype= PROTOCOL_BINARY_RAW_BYTES;

  rc= MEMCACHED_SUCCESS;
  for (uint32_t x= 0; x < memcached_server_count(ptr); x++) 
  {
    memcached_return_t rrc;

    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    if (instance->major_version != UINT8_MAX)
      continue;

    rrc= memcached_do(instance, request.bytes, sizeof(request.bytes), true);
    if (rrc != MEMCACHED_SUCCESS) 
    {
      memcached_io_reset(instance);
      rc= MEMCACHED_SOME_ERRORS;
      continue;
    }
  }

  for (uint32_t x= 0; x < memcached_server_count(ptr); x++) 
  {
    memcached_server_write_instance_st instance=
      memcached_server_instance_fetch(ptr, x);

    if (instance->major_version != UINT8_MAX)
      continue;

    if (memcached_server_response_count(instance) > 0) 
    {
      memcached_return_t rrc;
      char buffer[32];
      char *p;

      rrc= memcached_response(instance, buffer, sizeof(buffer), NULL);
      if (rrc != MEMCACHED_SUCCESS) 
      {
        memcached_io_reset(instance);
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->major_version= (uint8_t)strtol(buffer, &p, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->minor_version= (uint8_t)strtol(p + 1, &p, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

      instance->micro_version= (uint8_t)strtol(p + 1, NULL, 10);
      if (errno == ERANGE)
      {
        instance->major_version= instance->minor_version= instance->micro_version= UINT8_MAX;
        rc= MEMCACHED_SOME_ERRORS;
        continue;
      }

    }
  }

  return rc;
}
#endif
