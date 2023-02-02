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

#include <list>
#include <string>
#include <components/disksize/disksize.h>

REQUIRES_SERVICE_PLACEHOLDER(component_sys_variable_register);

REQUIRES_SERVICE_PLACEHOLDER(log_builtins);
REQUIRES_SERVICE_PLACEHOLDER(log_builtins_string);

REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
REQUIRES_SERVICE_PLACEHOLDER(mysql_security_context_options);
REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);
REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);

REQUIRES_MYSQL_MUTEX_SERVICE_PLACEHOLDER;

SERVICE_TYPE(log_builtins) * log_bi;
SERVICE_TYPE(log_builtins_string) * log_bs;

PSI_mutex_key key_mutex_disksize_data = 0;
PSI_mutex_info disksize_data_mutex[] = {
    {&key_mutex_disksize_data, "disksize_data", PSI_FLAG_SINGLETON, PSI_VOLATILITY_PERMANENT,
     "Disksize data, permanent mutex, singleton."}};

bool have_required_privilege(void *opaque_thd)
{
  // get the security context of the thread
  Security_context_handle ctx = nullptr;
  if (mysql_service_mysql_thd_security_context->get(opaque_thd, &ctx) || !ctx)
  {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "problem trying to get security context");
    return false;
  }

  if (mysql_service_global_grants_check->has_global_grant(
          ctx, PRIVILEGE_NAME, strlen(PRIVILEGE_NAME)))
    return true;

  return false;
}

static mysql_service_status_t disksize_service_init()
{
  mysql_service_status_t result = 0;

  log_bi = mysql_service_log_builtins;
  log_bs = mysql_service_log_builtins_string;

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "initializing...");
  mysql_mutex_init(key_mutex_disksize_data, &LOCK_disksize_data, nullptr);
  init_disksize_share(&disksize_st_share);
  share_list[0] = &disksize_st_share;
  if (mysql_service_pfs_plugin_table_v1->add_tables(&share_list[0],
                                                    share_list_count))
  {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "PFS table has NOT been registered successfully!");
    mysql_mutex_destroy(&LOCK_disksize_data);
    return 1;
  }
  else
  {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "PFS table has been registered successfully.");
  }
  // We need to add the content in the table

  return result;
}

static mysql_service_status_t disksize_service_deinit()
{
  mysql_service_status_t result = 0;

  cleanup_disksize_data();

  if (mysql_service_pfs_plugin_table_v1->delete_tables(&share_list[0],
                                                       share_list_count))
  {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Error while trying to remove PFS table");
    return 1;
  }
  else
  {
    LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "PFS table has been removed successfully.");
  }

  LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "uninstalled.");

  mysql_mutex_destroy(&LOCK_disksize_data);

  return result;
}

BEGIN_COMPONENT_PROVIDES(disksize_service)
END_COMPONENT_PROVIDES();

BEGIN_COMPONENT_REQUIRES(disksize_service)
REQUIRES_SERVICE(component_sys_variable_register),
    REQUIRES_SERVICE(log_builtins),
    REQUIRES_SERVICE(log_builtins_string),
    REQUIRES_SERVICE(mysql_thd_security_context),
    REQUIRES_SERVICE(mysql_security_context_options),
    REQUIRES_SERVICE(global_grants_check),
    REQUIRES_SERVICE(mysql_current_thread_reader),
    REQUIRES_SERVICE(mysql_runtime_error),
    REQUIRES_SERVICE(pfs_plugin_table_v1),
    REQUIRES_SERVICE_AS(pfs_plugin_column_bigint_v1, pfs_bigint),
    REQUIRES_SERVICE_AS(pfs_plugin_column_string_v2, pfs_string),
    REQUIRES_MYSQL_MUTEX_SERVICE,
    END_COMPONENT_REQUIRES();

/* A list of metadata to describe the Component. */
BEGIN_COMPONENT_METADATA(disksize_service)
METADATA("mysql.author", "Oracle Corporation"),
    METADATA("mysql.license", "GPL"), METADATA("mysql.dev", "lefred"),
    END_COMPONENT_METADATA();

/* Declaration of the Component. */
DECLARE_COMPONENT(disksize_service,
                  "mysql:disksize_service")
disksize_service_init,
    disksize_service_deinit END_DECLARE_COMPONENT();

/* Defines list of Components contained in this library. Note that for now
  we assume that library will have exactly one Component. */
DECLARE_LIBRARY_COMPONENTS &COMPONENT_REF(disksize_service)
    END_DECLARE_LIBRARY_COMPONENTS