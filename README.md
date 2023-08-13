# mysql-component-disksize

MySQL component for FOSDEM MySQL & Friends Devroom

See https://speakerdeck.com/lefred/extending-mysql-with-the-component-infrastructure

## Usage

```
mysql> install component "file://component_disksize";

mysql> select * from performance_schema.disks_size;
+-----------------+-----------------------------+-------------+-------------+
| DIR_NAME        | RELATED_VARIABLE            | FREE_SIZE   | TOTAL_SIZE  |
+-----------------+-----------------------------+-------------+-------------+
| /var/lib/mysql  | log_bin_basename            | 16416055296 | 31630573568 |
| /var/lib/mysql/ | datadir                     | 16416055296 | 31630573568 |
| /tmp            | tmpdir                      | 16416055296 | 31630573568 |
| ./              | innodb_undo_directory       | 16416055296 | 31630573568 |
| ./              | innodb_log_group_home_dir   | 16416055296 | 31630573568 |
| ./#innodb_temp/ | innodb_temp_tablespaces_dir | 16416055296 | 31630573568 |
| /tmp            | replica_load_tmpdir         | 16416055296 | 31630573568 |
+-----------------+-----------------------------+-------------+-------------+
7 rows in set (0.00 sec)
```


