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
  GNU General Public License, version 2.0, for more details.
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <components/disksize/disksize.h>

#include <array>

#define LOG_COMPONENT_TAG "disksize"

REQUIRES_SERVICE_PLACEHOLDER(pfs_plugin_table_v1);
REQUIRES_SERVICE_PLACEHOLDER_AS(pfs_plugin_column_bigint_v1, pfs_bigint);
REQUIRES_SERVICE_PLACEHOLDER_AS(pfs_plugin_column_string_v2, pfs_string);

static char *disksize_file_value;

// Declaration: Array of all variables we should parse
std::vector<std::string> variables_to_parse {
      "log_bin_basename",
      "datadir",
      "tmpdir",
      "innodb_undo_directory",
      "innodb_data_home_dir",
      "innodb_log_group_home_dir",
      "innodb_temp_tablespaces_dir",
      "innodb_tmpdir",
      "innodb_redo_log_archive_dirs",
      "replica_load_tmpdir"
      };

std::string getPathName(const std::string& s) {

   char sep = '/';

#ifdef _WIN32
   sep = '\\';
#endif

   size_t i = s.rfind(sep, s.length());
   if (i != std::string::npos) {
      return(s.substr(0, i));
   }

   return("");
}

/*
  DATA
*/

static std::array<Disksize_record *, DISKSIZE_MAX_ROWS> disksize_array;

/* Next available index for new record to be stored in global record array. */
static size_t disksize_next_available_index = 0;

void init_disksize_data() {
  mysql_mutex_lock(&LOCK_disksize_data);
  disksize_next_available_index = 0;
  disksize_array.fill(nullptr);
  mysql_mutex_unlock(&LOCK_disksize_data);
}

void cleanup_disksize_data() {
  mysql_mutex_lock(&LOCK_disksize_data);
  for (Disksize_record *disksize : disksize_array) {
    delete disksize;
  }
  mysql_mutex_unlock(&LOCK_disksize_data);
}

/*
  DATA collection
*/

void addDisksize_element(std::string disksize_dir_name, 
                      std::string disksize_related_variable,
                      PSI_ulonglong disksize_free_size,
                      PSI_ulonglong disksize_total_size) {
  size_t index;
  Disksize_record *record;

  mysql_mutex_lock(&LOCK_disksize_data);

  index = disksize_next_available_index++ % DISKSIZE_MAX_ROWS;
  record = disksize_array[index];

  if (record != nullptr) {
    delete record;
  }

  record = new Disksize_record;
  record->disksize_dir_name = disksize_dir_name;
  record->disksize_related_variable = disksize_related_variable;
  record->disksize_dir_size_free = disksize_free_size;
  record->disksize_dir_size_total = disksize_total_size;

  disksize_array[index] = record;

  mysql_mutex_unlock(&LOCK_disksize_data);
}

/*
  DATA access (performance schema table)
*/

/* Collection of table shares to be added to performance schema */
PFS_engine_table_share_proxy *share_list[1] = {nullptr};
unsigned int share_list_count = 1;

/* Global share pointer for a table */
PFS_engine_table_share_proxy disksize_st_share;

int disksize_delete_all_rows(void) {
  cleanup_disksize_data();
  return 0;
}

PSI_table_handle *disksize_open_table(PSI_pos **pos) {
  disksize_file_value = nullptr;
  char *value = nullptr;
  char buffer_for_value[1024];
  size_t value_length;
  char msgbuf[1024];

  init_disksize_data();
  MYSQL_THD thd;

  mysql_service_mysql_current_thread_reader->get(&thd);
  if(!have_required_privilege(thd)) {
       mysql_error_service_printf(
            ER_SPECIFIC_ACCESS_DENIED_ERROR, 0,
            PRIVILEGE_NAME);
  } else {
    int j = 0;
    for (int i = 0; i < int(variables_to_parse.size()); i++)
    {
        value = &buffer_for_value[0];
        value_length = sizeof(buffer_for_value) - 1;

        long long unsigned int size;
        long long unsigned int free;
        struct statvfs buf;

        const char *var_to_get = variables_to_parse.operator[](i).c_str();

        if (mysql_service_component_sys_variable_register->get_variable(
                "mysql_server", var_to_get, (void **)&value, &value_length))
        {
            sprintf(msgbuf, "Could not get value of variable [%s]", var_to_get);
            LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, msgbuf);
            continue;
        }
        if (strlen(value) > 0)
        {
            std::string path;
            path = value;
            // Let's check if we are looking for log_bin_basename
            if (strcmp(var_to_get, "log_bin_basename") == 0)
            {
            path = getPathName(value);
            }
            // Now let's get the info from disk

            if (statvfs(path.c_str(), &buf) == -1)
            {
              sprintf(msgbuf, "OS File access problem to %s", path.c_str());
              LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, msgbuf);
              continue;
            }
        }
        else
            continue;

        size = (long long unsigned int)buf.f_blocks * buf.f_bsize;
        free = (long long unsigned int)buf.f_bavail * buf.f_bsize;

        PSI_ubigint psi_size_free;
        PSI_ubigint psi_size_total;

        psi_size_free = {free, false};
        psi_size_total = {size, false};

        addDisksize_element(value, var_to_get, psi_size_free, psi_size_total);
        j++;
    }
  }
  Disksize_Table_Handle *temp = new Disksize_Table_Handle();
  *pos = (PSI_pos *)(&temp->m_pos);

  return (PSI_table_handle *)temp;
}

void disksize_close_table(PSI_table_handle *handle) {
  Disksize_Table_Handle *temp = (Disksize_Table_Handle *)handle;
  delete temp;
}

static void copy_record_disksize(Disksize_record *dest, const Disksize_record *source) {
  dest->disksize_dir_name = source->disksize_dir_name;
  dest->disksize_related_variable = source->disksize_related_variable;
  dest->disksize_dir_size_free = source->disksize_dir_size_free;
  dest->disksize_dir_size_total = source->disksize_dir_size_total;
  return;
}

/* Define implementation of PFS_engine_table_proxy. */
int disksize_rnd_next(PSI_table_handle *handle) {
  Disksize_Table_Handle *h = (Disksize_Table_Handle *)handle;
  h->m_pos.set_at(&h->m_next_pos);
  size_t index = h->m_pos.get_index();

  if (index < disksize_array.size()) {
    Disksize_record *record = disksize_array[index];
    if (record != nullptr) {
      /* Make the current row from records_array buffer */
      copy_record_disksize(&h->current_row, record);
      h->m_next_pos.set_after(&h->m_pos);
      return 0;
    }
  }

  return PFS_HA_ERR_END_OF_FILE;
}

int disksize_rnd_init(PSI_table_handle *, bool) { return 0; }

/* Set position of a cursor on a specific index */
int disksize_rnd_pos(PSI_table_handle *handle) {
  Disksize_Table_Handle *h = (Disksize_Table_Handle *)handle;
  size_t index = h->m_pos.get_index();

  if (index < disksize_array.size()) {
    Disksize_record *record = disksize_array[index];

    if (record != nullptr) {
      /* Make the current row from records_array buffer */
      copy_record_disksize(&h->current_row, record);
    }
  }

  return 0;
}

/* Reset cursor position */
void disksize_reset_position(PSI_table_handle *handle) {
  Disksize_Table_Handle *h = (Disksize_Table_Handle *)handle;
  h->m_pos.reset();
  h->m_next_pos.reset();
  return;
}

/* Read current row from the current_row and display them in the table */
int disksize_read_column_value(PSI_table_handle *handle, PSI_field *field,
                            unsigned int index) {
  Disksize_Table_Handle *h = (Disksize_Table_Handle *)handle;

  switch (index) {
    case 0: /* DIR_NAME */
        pfs_string->set_varchar_utf8mb4(
            field, h->current_row.disksize_dir_name.c_str());
        break;
    case 1: /* RELATED_VARIABLE */
        pfs_string->set_varchar_utf8mb4(
            field, h->current_row.disksize_related_variable.c_str());
        break;
    case 2: /* FREE_SIZE */
        pfs_bigint->set_unsigned(field, h->current_row.disksize_dir_size_free);
        break;
    case 3: /* TOTAL_SIZE */
        pfs_bigint->set_unsigned(field, h->current_row.disksize_dir_size_total);
        break;
    default: /* We should never reach here */
        //assert(0);
        break;
  }
  return 0;
}

unsigned long long disksize_get_row_count(void) { return DISKSIZE_MAX_ROWS; }

void init_disksize_share(PFS_engine_table_share_proxy *share) {
  /* Instantiate and initialize PFS_engine_table_share_proxy */
  share->m_table_name = "disks_size";
  share->m_table_name_length = 10;
  share->m_table_definition =
         "DIR_NAME char(255) not null, RELATED_VARIABLE char(60) not null, "
         "FREE_SIZE bigint unsigned, TOTAL_SIZE bigint unsigned, PRIMARY KEY(DIR_NAME)";
  share->m_ref_length = sizeof(Disksize_POS);
  share->m_acl = READONLY;
  share->get_row_count = disksize_get_row_count;
  share->delete_all_rows = nullptr; /* READONLY TABLE */

  /* Initialize PFS_engine_table_proxy */
  share->m_proxy_engine_table = {disksize_rnd_next, disksize_rnd_init, disksize_rnd_pos,
                                 nullptr, nullptr, nullptr,
                                 disksize_read_column_value, disksize_reset_position,
                                 /* READONLY TABLE */
                                 nullptr, /* write_column_value */
                                 nullptr, /* write_row_values */
                                 nullptr, /* update_column_value */
                                 nullptr, /* update_row_values */
                                 nullptr, /* delete_row_values */
                                 disksize_open_table, disksize_close_table};
}