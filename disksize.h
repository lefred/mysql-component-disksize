/* Copyright (c) 2017, 2023, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 3.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#define LOG_COMPONENT_TAG "disksize"

#include <string>
#include <vector>
#include <mysql/components/component_implementation.h>
#include <mysql/components/service_implementation.h>
#include <mysql/components/services/pfs_plugin_table_service.h>
#include <mysql/components/services/component_sys_var_service.h>
#include <mysql/components/services/mysql_current_thread_reader.h>
#include <mysql/components/services/security_context.h>
#include <mysql/components/services/mysql_runtime_error_service.h>
#include <mysql/components/services/log_builtins.h> /* LogComponentErr */
#include <mysqld_error.h>                           /* Errors */
#include <mysql/components/services/mysql_mutex.h>

#ifndef _WIN32
#include <sys/statvfs.h>
#endif

extern REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_register);
extern REQUIRES_SERVICE_PLACEHOLDER(log_builtins);
extern REQUIRES_SERVICE_PLACEHOLDER(log_builtins_string);
extern REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_register);

extern REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_security_context_options);
extern REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);

extern REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);

extern REQUIRES_SERVICE_PLACEHOLDER(pfs_plugin_table_v1);
extern REQUIRES_SERVICE_PLACEHOLDER_AS(pfs_plugin_column_bigint_v1, pfs_bigint);
extern REQUIRES_SERVICE_PLACEHOLDER_AS(pfs_plugin_column_string_v2, pfs_string);

extern REQUIRES_MYSQL_MUTEX_SERVICE_PLACEHOLDER;

extern SERVICE_TYPE(log_builtins) * log_bi;
extern SERVICE_TYPE(log_builtins_string) * log_bs;

#define PRIVILEGE_NAME "SENSITIVE_VARIABLES_OBSERVER"


void init_disksize_data();
void cleanup_disksize_data();
extern void addDisksize_element(std::string disksize_dir_name, 
                      std::string disksize_related_variable,
                      PSI_ulonglong disksize_free_size,
                      PSI_ulonglong disksize_total_size); 

extern bool have_required_privilege(void *opaque_thd);

int disksize_prepare_insert_row();

/* Maximum number of rows in the table */
#define DISKSIZE_MAX_ROWS  10

/* Global share pointer for pfs_example_disksize table */
extern PFS_engine_table_share_proxy disksize_st_share;

/* A structure to denote a single row of the table. */
struct Disksize_record {
  std::string disksize_dir_name;
  std::string disksize_related_variable;
  PSI_ubigint disksize_dir_size_free;
  PSI_ubigint disksize_dir_size_total;
};

class Disksize_POS {
 private:
  unsigned int m_index = 0;

 public:
  ~Disksize_POS() = default;
  Disksize_POS() { m_index = 0; }

  void reset() { m_index = 0; }

  unsigned int get_index() { return m_index; }

  void set_at(unsigned int index) { m_index = index; }

  void set_at(Disksize_POS *pos) { m_index = pos->m_index; }

  void set_after(Disksize_POS *pos) { m_index = pos->m_index + 1; }
};

struct Disksize_Table_Handle {
  /* Current position instance */
  Disksize_POS m_pos;
  /* Next position instance */
  Disksize_POS m_next_pos;

  /* Current row for the table */
  Disksize_record current_row;

  /* Index indicator */
  unsigned int index_num;
};

void init_disksize_share(PFS_engine_table_share_proxy *share);

extern PFS_engine_table_share_proxy disksize_st_share;

extern PFS_engine_table_share_proxy *share_list[];
extern unsigned int share_list_count;

static mysql_mutex_t LOCK_disksize_data;
extern PSI_mutex_key key_mutex_disksize_data;
extern PSI_mutex_info disksize_data_mutex[];
