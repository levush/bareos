/*
   BAREOS® - Backup Archiving REcovery Open Sourced

   Copyright (C) 2000-2009 Free Software Foundation Europe e.V.
   Copyright (C) 2011-2012 Planets Communications B.V.
   Copyright (C) 2013-2013 Bareos GmbH & Co. KG

   This program is Free Software; you can redistribute it and/or
   modify it under the terms of version three of the GNU Affero General Public
   License as published by the Free Software Foundation and included
   in the file LICENSE.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
   Affero General Public License for more details.

   You should have received a copy of the GNU Affero General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.
*/
/*
 * Configuration file parser for Bareos Storage daemon
 *
 * Kern Sibbald, March MM
 */

#include "bareos.h"
#include "stored.h"

/*
 * First and last resource ids
 */
int32_t r_first = R_FIRST;
int32_t r_last  = R_LAST;
static RES *sres_head[R_LAST - R_FIRST + 1];
RES **res_head = sres_head;

/*
 * Forward referenced subroutines
 */
static void store_authtype(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_clearpassword(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_devtype(LEX *lc, RES_ITEM *item, int index, int pass);
static void store_maxblocksize(LEX *lc, RES_ITEM *item, int index, int pass);

/*
 * We build the current resource here statically,
 * then move it to dynamic memory
 */
#if defined(_MSC_VER)
extern "C" { // work around visual compiler mangling variables
    URES res_all;
}
#else
URES res_all;
#endif
int32_t res_all_size = sizeof(res_all);

/*
 * Definition of records permitted within each
 * resource with the routine to process the record
 * information.
 */

/*
 * Globals for the Storage daemon.
 */
static RES_ITEM store_items[] = {
   { "name", store_name, ITEM(res_store.hdr.name), 0, ITEM_REQUIRED, NULL },
   { "description", store_str, ITEM(res_store.hdr.desc), 0, 0, NULL },
   { "sdaddress", store_addresses_address, ITEM(res_store.sdaddrs), 0, ITEM_DEFAULT, SD_DEFAULT_PORT },
   { "sdaddresses", store_addresses, ITEM(res_store.sdaddrs), 0, ITEM_DEFAULT, SD_DEFAULT_PORT },
   { "messages", store_res, ITEM(res_store.messages), R_MSGS, 0, NULL },
   { "sdport", store_addresses_port, ITEM(res_store.sdaddrs), 0, ITEM_DEFAULT, SD_DEFAULT_PORT },
   { "workingdirectory", store_dir, ITEM(res_store.working_directory), 0, ITEM_DEFAULT, _PATH_BAREOS_WORKINGDIR },
   { "piddirectory", store_dir, ITEM(res_store.pid_directory), 0, ITEM_DEFAULT, _PATH_BAREOS_PIDDIR },
   { "subsysdirectory", store_dir, ITEM(res_store.subsys_directory), 0, 0, NULL },
   { "plugindirectory", store_dir, ITEM(res_store.plugin_directory), 0, 0, NULL },
   { "scriptsdirectory", store_dir, ITEM(res_store.scripts_directory), 0, 0, NULL },
   { "maximumconcurrentjobs", store_pint32, ITEM(res_store.max_concurrent_jobs), 0, ITEM_DEFAULT, "20" },
   { "heartbeatinterval", store_time, ITEM(res_store.heartbeat_interval), 0, ITEM_DEFAULT, "0" },
   { "tlsauthenticate", store_bool, ITEM(res_store.tls_authenticate), 0, 0, NULL },
   { "tlsenable", store_bool, ITEM(res_store.tls_enable), 0, 0, NULL },
   { "tlsrequire", store_bool, ITEM(res_store.tls_require), 0, 0, NULL },
   { "tlsverifypeer", store_bool, ITEM(res_store.tls_verify_peer), 1, ITEM_DEFAULT, "true" },
   { "tlscacertificatefile", store_dir, ITEM(res_store.tls_ca_certfile), 0, 0, NULL },
   { "tlscacertificatedir", store_dir, ITEM(res_store.tls_ca_certdir), 0, 0, NULL },
   { "tlscertificate", store_dir, ITEM(res_store.tls_certfile), 0, 0, NULL },
   { "tlskey", store_dir, ITEM(res_store.tls_keyfile), 0, 0, NULL },
   { "tlsdhfile", store_dir, ITEM(res_store.tls_dhfile), 0, 0, NULL },
   { "tlsallowedcn", store_alist_str, ITEM(res_store.tls_allowed_cns), 0, 0, NULL },
   { "clientconnectwait", store_time, ITEM(res_store.client_wait), 0, ITEM_DEFAULT, "180" /* 30 * 60 */ },
   { "verid", store_str, ITEM(res_store.verid), 0, 0, NULL },
   { "compatible", store_bool, ITEM(res_store.compatible), 0, ITEM_DEFAULT, "true" },
   { "ndmpenable", store_bool, ITEM(res_store.ndmp_enable), 0, 0, NULL },
   { "ndmpsnooping", store_bool, ITEM(res_store.ndmp_snooping), 0, 0, NULL },
   { "ndmploglevel", store_pint32, ITEM(res_store.ndmploglevel), 0, ITEM_DEFAULT, "4" },
   { "ndmpaddress", store_addresses_address, ITEM(res_store.ndmpaddrs), 0, ITEM_DEFAULT, "10000" },
   { "ndmpaddresses", store_addresses, ITEM(res_store.ndmpaddrs), 0, ITEM_DEFAULT, "10000" },
   { "ndmpport", store_addresses_port, ITEM(res_store.ndmpaddrs), 0, ITEM_DEFAULT, "10000" },
   { NULL, NULL, { 0 }, 0, 0, NULL }
};

/*
 * Directors that can speak to the Storage daemon
 */
static RES_ITEM dir_items[] = {
   { "name", store_name, ITEM(res_dir.hdr.name), 0, ITEM_REQUIRED, NULL },
   { "description", store_str, ITEM(res_dir.hdr.desc), 0, 0, NULL },
   { "password", store_clearpassword, ITEM(res_dir.password), 0, ITEM_REQUIRED, NULL },
   { "monitor", store_bool, ITEM(res_dir.monitor), 0, 0, NULL },
   { "tlsauthenticate", store_bool, ITEM(res_dir.tls_authenticate), 0, 0, NULL },
   { "tlsenable", store_bool, ITEM(res_dir.tls_enable), 0, 0, NULL },
   { "tlsrequire", store_bool, ITEM(res_dir.tls_require), 0, 0, NULL },
   { "tlsverifypeer", store_bool, ITEM(res_dir.tls_verify_peer), 1, ITEM_DEFAULT, "true" },
   { "tlscacertificatefile", store_dir, ITEM(res_dir.tls_ca_certfile), 0, 0, NULL },
   { "tlscacertificatedir", store_dir, ITEM(res_dir.tls_ca_certdir), 0, 0, NULL },
   { "tlscertificate", store_dir, ITEM(res_dir.tls_certfile), 0, 0, NULL },
   { "tlskey", store_dir, ITEM(res_dir.tls_keyfile), 0, 0, NULL },
   { "tlsdhfile", store_dir, ITEM(res_dir.tls_dhfile), 0, 0, NULL },
   { "tlsallowedcn", store_alist_str, ITEM(res_dir.tls_allowed_cns), 0, 0, NULL },
   { "keyencryptionkey", store_clearpassword, ITEM(res_dir.keyencrkey), 1, 0, NULL },
   { NULL, NULL, { 0 }, 0, 0, NULL }
};

/*
 * NDMP DMA's that can speak to the Storage daemon
 */
static RES_ITEM ndmp_items[] = {
   { "name", store_name, ITEM(res_ndmp.hdr.name), 0, ITEM_REQUIRED, 0 },
   { "description", store_str, ITEM(res_ndmp.hdr.desc), 0, 0, 0 },
   { "username", store_str, ITEM(res_ndmp.username), 0, ITEM_REQUIRED, 0 },
   { "password", store_clearpassword, ITEM(res_ndmp.password), 0, ITEM_REQUIRED, 0 },
   { "authtype", store_authtype, ITEM(res_ndmp.AuthType), 0, ITEM_DEFAULT, "None" },
   { "loglevel", store_pint32, ITEM(res_ndmp.LogLevel), 0, ITEM_DEFAULT, "4" },
   { NULL, NULL, { 0 }, 0, 0, 0 }
};

/*
 * Device definition
 */
static RES_ITEM dev_items[] = {
   { "name", store_name, ITEM(res_dev.hdr.name), 0, ITEM_REQUIRED, NULL },
   { "description", store_str, ITEM(res_dev.hdr.desc), 0, 0, NULL },
   { "mediatype", store_strname,ITEM(res_dev.media_type), 0, ITEM_REQUIRED, NULL },
   { "devicetype", store_devtype,ITEM(res_dev.dev_type), 0, 0, NULL },
   { "archivedevice", store_strname,ITEM(res_dev.device_name), 0, ITEM_REQUIRED, NULL },
   { "hardwareendoffile", store_bit, ITEM(res_dev.cap_bits), CAP_EOF, ITEM_DEFAULT, "on" },
   { "hardwareendofmedium", store_bit, ITEM(res_dev.cap_bits), CAP_EOM, ITEM_DEFAULT, "on" },
   { "backwardspacerecord", store_bit, ITEM(res_dev.cap_bits), CAP_BSR, ITEM_DEFAULT, "on" },
   { "backwardspacefile", store_bit, ITEM(res_dev.cap_bits), CAP_BSF, ITEM_DEFAULT, "on" },
   { "bsfateom", store_bit, ITEM(res_dev.cap_bits), CAP_BSFATEOM, ITEM_DEFAULT, "off" },
   { "twoeof", store_bit, ITEM(res_dev.cap_bits), CAP_TWOEOF, ITEM_DEFAULT, "off" },
   { "forwardspacerecord", store_bit, ITEM(res_dev.cap_bits), CAP_FSR, ITEM_DEFAULT, "on" },
   { "forwardspacefile", store_bit, ITEM(res_dev.cap_bits), CAP_FSF, ITEM_DEFAULT, "on" },
   { "fastforwardspacefile", store_bit, ITEM(res_dev.cap_bits), CAP_FASTFSF, ITEM_DEFAULT, "on" },
   { "removablemedia", store_bit, ITEM(res_dev.cap_bits), CAP_REM, ITEM_DEFAULT, "on" },
   { "randomaccess", store_bit, ITEM(res_dev.cap_bits), CAP_RACCESS, 0, "off" },
   { "automaticmount", store_bit, ITEM(res_dev.cap_bits), CAP_AUTOMOUNT, ITEM_DEFAULT, "off" },
   { "labelmedia", store_bit, ITEM(res_dev.cap_bits), CAP_LABEL, ITEM_DEFAULT, "off" },
   { "alwaysopen", store_bit, ITEM(res_dev.cap_bits), CAP_ALWAYSOPEN, ITEM_DEFAULT, "on" },
   { "autochanger", store_bit, ITEM(res_dev.cap_bits), CAP_AUTOCHANGER, ITEM_DEFAULT, "off" },
   { "closeonpoll", store_bit, ITEM(res_dev.cap_bits), CAP_CLOSEONPOLL, ITEM_DEFAULT, "off" },
   { "blockpositioning", store_bit, ITEM(res_dev.cap_bits), CAP_POSITIONBLOCKS, ITEM_DEFAULT, "on" },
   { "usemtiocget", store_bit, ITEM(res_dev.cap_bits), CAP_MTIOCGET, ITEM_DEFAULT, "on" },
   { "checklabels", store_bit, ITEM(res_dev.cap_bits), CAP_CHECKLABELS, ITEM_DEFAULT, "off" },
   { "requiresmount", store_bit, ITEM(res_dev.cap_bits), CAP_REQMOUNT, ITEM_DEFAULT, "off" },
   { "offlineonunmount", store_bit, ITEM(res_dev.cap_bits), CAP_OFFLINEUNMOUNT, ITEM_DEFAULT, "off" },
   { "blockchecksum", store_bit, ITEM(res_dev.cap_bits), CAP_BLOCKCHECKSUM, ITEM_DEFAULT, "on" },
   { "autoselect", store_bool, ITEM(res_dev.autoselect), 1, ITEM_DEFAULT, "true" },
   { "changerdevice", store_strname,ITEM(res_dev.changer_name), 0, 0, NULL },
   { "changercommand", store_strname,ITEM(res_dev.changer_command), 0, 0, NULL },
   { "alertcommand", store_strname,ITEM(res_dev.alert_command), 0, 0, NULL },
   { "maximumchangerwait", store_time, ITEM(res_dev.max_changer_wait), 0, ITEM_DEFAULT, "300" /* 5 * 60 */ },
   { "maximumopenwait", store_time, ITEM(res_dev.max_open_wait), 0, ITEM_DEFAULT, "300" /* 5 * 60 */ },
   { "maximumopenvolumes", store_pint32, ITEM(res_dev.max_open_vols), 0, ITEM_DEFAULT, "1" },
   { "maximumnetworkbuffersize", store_pint32, ITEM(res_dev.max_network_buffer_size), 0, 0, NULL },
   { "volumepollinterval", store_time, ITEM(res_dev.vol_poll_interval), 0, ITEM_DEFAULT, "300" /* 5 * 60 */ },
   { "maximumrewindwait", store_time, ITEM(res_dev.max_rewind_wait), 0, ITEM_DEFAULT, "300" /* 5 * 60 */ },
   { "minimumblocksize", store_pint32, ITEM(res_dev.min_block_size), 0, 0, NULL },
   { "maximumblocksize", store_maxblocksize, ITEM(res_dev.max_block_size), 0, 0, NULL },
   { "maximumvolumesize", store_size64, ITEM(res_dev.max_volume_size), 0, ITEM_DEPRECATED, NULL },
   { "maximumfilesize", store_size64, ITEM(res_dev.max_file_size), 0, ITEM_DEFAULT, "1000000000" },
   { "volumecapacity", store_size64, ITEM(res_dev.volume_capacity), 0, 0, NULL },
   { "maximumconcurrentjobs", store_pint32, ITEM(res_dev.max_concurrent_jobs), 0, 0, NULL },
   { "spooldirectory", store_dir, ITEM(res_dev.spool_directory), 0, 0, NULL },
   { "maximumspoolsize", store_size64, ITEM(res_dev.max_spool_size), 0, 0, NULL },
   { "maximumjobspoolsize", store_size64, ITEM(res_dev.max_job_spool_size), 0, 0, NULL },
   { "driveindex", store_pint32, ITEM(res_dev.drive_index), 0, 0, NULL },
   { "maximumpartsize", store_size64, ITEM(res_dev.max_part_size), 0, ITEM_DEPRECATED, NULL },
   { "mountpoint", store_strname,ITEM(res_dev.mount_point), 0, 0, NULL },
   { "mountcommand", store_strname,ITEM(res_dev.mount_command), 0, 0, NULL },
   { "unmountcommand", store_strname,ITEM(res_dev.unmount_command), 0, 0, NULL },
   { "writepartcommand", store_strname,ITEM(res_dev.write_part_command), 0, ITEM_DEPRECATED, NULL },
   { "freespacecommand", store_strname,ITEM(res_dev.free_space_command), 0, ITEM_DEPRECATED, NULL },
   { "labeltype", store_label, ITEM(res_dev.label_type), 0, 0, NULL },
   { "norewindonclose", store_bool, ITEM(res_dev.norewindonclose), 0, 0, NULL },
   { "drivecryptoenabled", store_bool, ITEM(res_dev.drive_crypto_enabled), 0, 0, NULL },
   { "querycryptostatus", store_bool, ITEM(res_dev.query_crypto_status), 0, 0, NULL },
   { NULL, NULL, { 0 }, 0, 0, NULL }
};

/*
 * Autochanger definition
 */
static RES_ITEM changer_items[] = {
   { "name", store_name, ITEM(res_changer.hdr.name), 0, ITEM_REQUIRED, NULL },
   { "description", store_str, ITEM(res_changer.hdr.desc), 0, 0, NULL },
   { "device", store_alist_res, ITEM(res_changer.device), R_DEVICE, ITEM_REQUIRED, NULL },
   { "changerdevice", store_strname, ITEM(res_changer.changer_name), 0, ITEM_REQUIRED, NULL },
   { "changercommand", store_strname, ITEM(res_changer.changer_command), 0, ITEM_REQUIRED, NULL },
   { NULL, NULL, { 0 }, 0, 0, NULL }
};

// { "mountanonymousvolumes", store_bit, ITEM(res_dev.cap_bits), CAP_ANONVOLS, ITEM_DEFAULT, "off" },

/*
 * Message resource
 */
extern RES_ITEM msgs_items[];

/*
 * This is the master resource definition
 */
RES_TABLE resources[] = {
   { "director", dir_items, R_DIRECTOR },
   { "ndmp", ndmp_items, R_NDMP },
   { "storage", store_items, R_STORAGE },
   { "device", dev_items, R_DEVICE },
   { "messages", msgs_items, R_MSGS },
   { "autochanger", changer_items, R_AUTOCHANGER },
   { NULL, NULL, 0 }
};

/*
 * Authentication methods
 */
struct s_kw authmethods[] = {
   { "None", AT_NONE },
   { "Clear", AT_CLEAR },
   { "MD5", AT_MD5 },
   { NULL, 0 }
};

/*
 * Device types
 *
 * device type, device code = token
 */
static s_kw dev_types[] = {
   { "file", B_FILE_DEV },
   { "tape", B_TAPE_DEV },
   { "fifo", B_FIFO_DEV },
   { "vtl", B_VTL_DEV },
   { "vtape", B_VTAPE_DEV },
   { NULL, 0 }
};

/*
 * Store authentication type (Mostly for NDMP like clear or MD5).
 */
void store_authtype(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /*
    * Store the type both pass 1 and pass 2
    */
   for (i = 0; authmethods[i].name; i++) {
      if (bstrcasecmp(lc->str, authmethods[i].name)) {
         *(uint32_t *)(item->value) = authmethods[i].token;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Authentication Type keyword, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store password either clear if for NDMP or MD5 hashed for native.
 */
static void store_clearpassword(LEX *lc, RES_ITEM *item, int index, int pass)
{
   switch (res_all.hdr.rcode) {
   case R_DIRECTOR:
      /*
       * As we need to store both clear and MD5 hashed within the same
       * resource class we use the item->code as a hint default is 0
       * and for clear we need a code of 1.
       */
      switch (item->code) {
      case 1:
         store_str(lc, item, index, pass);
         break;
      default:
         store_password(lc, item, index, pass);
         break;
      }
      break;
   case R_NDMP:
      store_str(lc, item, index, pass);
      break;
   default:
      store_password(lc, item, index, pass);
      break;
   }
}
/*
 * Store Device Type (File, FIFO, Tape)
 */
static void store_devtype(LEX *lc, RES_ITEM *item, int index, int pass)
{
   int i;

   lex_get_token(lc, T_NAME);
   /*
    * Store the label pass 2 so that type is defined
    */
   for (i = 0; dev_types[i].name; i++) {
      if (bstrcasecmp(lc->str, dev_types[i].name)) {
         *(uint32_t *)(item->value) = dev_types[i].token;
         i = 0;
         break;
      }
   }
   if (i != 0) {
      scan_err1(lc, _("Expected a Device Type keyword, got: %s"), lc->str);
   }
   scan_to_eol(lc);
   set_bit(index, res_all.hdr.item_present);
}

/*
 * Store Maximum Block Size, and check it is not greater than MAX_BLOCK_LENGTH
 */
static void store_maxblocksize(LEX *lc, RES_ITEM *item, int index, int pass)
{
   store_size32(lc, item, index, pass);
   if (*(uint32_t *)(item->value) > MAX_BLOCK_LENGTH) {
      scan_err2(lc, _("Maximum Block Size configured value %u is greater than allowed maximum: %u"),
         *(uint32_t *)(item->value), MAX_BLOCK_LENGTH );
   }
}

/*
 * Dump contents of resource
 */
void dump_resource(int type, RES *reshdr, void sendit(void *sock, const char *fmt, ...), void *sock)
{
   URES *res = (URES *)reshdr;
   char buf[1000];
   int recurse = 1;
   IPADDR *addr;
   if (res == NULL) {
      sendit(sock, _("Warning: no \"%s\" resource (%d) defined.\n"), res_to_str(type), type);
      return;
   }
   sendit(sock, _("dump_resource type=%d\n"), type);
   if (type < 0) { /* no recursion */
      type = - type;
      recurse = 0;
   }
   switch (type) {
   case R_DIRECTOR:
      sendit(sock, "Director: name=%s\n", res->res_dir.hdr.name);
      break;
   case R_NDMP:
      sendit(sock, "NDMP DMA: name=%s\n", res->res_dir.hdr.name);
      break;
   case R_STORAGE:
      sendit(sock, "Storage: name=%s SDaddr=%s SDport=%d HB=%s\n",
             res->res_store.hdr.name,
             NPRT(get_first_address(res->res_store.sdaddrs, buf, sizeof(buf))),
             get_first_port_host_order(res->res_store.sdaddrs),
             edit_utime(res->res_store.heartbeat_interval, buf, sizeof(buf)));
      if (res->res_store.sdaddrs) {
         foreach_dlist(addr, res->res_store.sdaddrs) {
            sendit(sock, "        SDaddr=%s SDport=%d\n",
                   addr->get_address(buf, sizeof(buf)),
                   addr->get_port_host_order());
         }
      }
      if (res->res_store.sdaddrs) {
         foreach_dlist(addr, res->res_store.ndmpaddrs) {
            sendit(sock, "        NDMPaddr=%s NDMPport=%d\n",
                   addr->get_address(buf, sizeof(buf)),
                   addr->get_port_host_order());
         }
      }
      break;
   case R_DEVICE:
      sendit(sock, "Device: name=%s MediaType=%s Device=%s LabelType=%d\n",
             res->res_dev.hdr.name,
             res->res_dev.media_type, res->res_dev.device_name,
             res->res_dev.label_type);
      sendit(sock, "        rew_wait=%" lld " min_bs=%d max_bs=%d chgr_wait=%" lld "\n",
             res->res_dev.max_rewind_wait, res->res_dev.min_block_size,
             res->res_dev.max_block_size, res->res_dev.max_changer_wait);
      sendit(sock, "        max_jobs=%d max_files=%" lld " max_size=%" lld "\n",
             res->res_dev.max_volume_jobs, res->res_dev.max_volume_files,
             res->res_dev.max_volume_size);
      sendit(sock, "        max_file_size=%" lld " capacity=%" lld "\n",
             res->res_dev.max_file_size, res->res_dev.volume_capacity);
      sendit(sock, "        spool_directory=%s\n", NPRT(res->res_dev.spool_directory));
      sendit(sock, "        max_spool_size=%" lld " max_job_spool_size=%" lld "\n",
             res->res_dev.max_spool_size, res->res_dev.max_job_spool_size);
      if (res->res_dev.changer_res) {
         sendit(sock, "         changer=%p\n", res->res_dev.changer_res);
      }
      bstrncpy(buf, "        ", sizeof(buf));
      if (res->res_dev.cap_bits & CAP_EOF) {
         bstrncat(buf, "CAP_EOF ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_BSR) {
         bstrncat(buf, "CAP_BSR ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_BSF) {
         bstrncat(buf, "CAP_BSF ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_FSR) {
         bstrncat(buf, "CAP_FSR ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_FSF) {
         bstrncat(buf, "CAP_FSF ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_EOM) {
         bstrncat(buf, "CAP_EOM ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_REM) {
         bstrncat(buf, "CAP_REM ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_RACCESS) {
         bstrncat(buf, "CAP_RACCESS ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_AUTOMOUNT) {
         bstrncat(buf, "CAP_AUTOMOUNT ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_LABEL) {
         bstrncat(buf, "CAP_LABEL ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_ANONVOLS) {
         bstrncat(buf, "CAP_ANONVOLS ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_ALWAYSOPEN) {
         bstrncat(buf, "CAP_ALWAYSOPEN ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_CHECKLABELS) {
         bstrncat(buf, "CAP_CHECKLABELS ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_REQMOUNT) {
         bstrncat(buf, "CAP_REQMOUNT ", sizeof(buf));
      }
      if (res->res_dev.cap_bits & CAP_OFFLINEUNMOUNT) {
         bstrncat(buf, "CAP_OFFLINEUNMOUNT ", sizeof(buf));
      }
      bstrncat(buf, "\n", sizeof(buf));
      sendit(sock, buf);
      break;
   case R_AUTOCHANGER:
      DEVRES *dev;
      sendit(sock, "Changer: name=%s Changer_devname=%s\n      Changer_cmd=%s\n",
             res->res_changer.hdr.name,
             res->res_changer.changer_name, res->res_changer.changer_command);
      foreach_alist(dev, res->res_changer.device) {
         sendit(sock, "   --->Device: name=%s\n", dev->hdr.name);
      }
      bstrncat(buf, "\n", sizeof(buf));
      sendit(sock, buf);
      break;
   case R_MSGS:
      sendit(sock, "Messages: name=%s\n", res->res_msgs.hdr.name);
      if (res->res_msgs.mail_cmd)
         sendit(sock, "      mailcmd=%s\n", res->res_msgs.mail_cmd);
      if (res->res_msgs.operator_cmd)
         sendit(sock, "      opcmd=%s\n", res->res_msgs.operator_cmd);
      break;
   default:
      sendit(sock, _("Warning: unknown resource type %d\n"), type);
      break;
   }
   if (recurse && res->res_dir.hdr.next)
      dump_resource(type, (RES *)res->res_dir.hdr.next, sendit, sock);
}

/*
 * Free memory of resource.
 * NB, we don't need to worry about freeing any references
 * to other resources as they will be freed when that
 * resource chain is traversed.  Mainly we worry about freeing
 * allocated strings (names).
 */
void free_resource(RES *sres, int type)
{
   RES *nres;
   URES *res = (URES *)sres;

   if (res == NULL)
      return;

   /*
    * Common stuff -- free the resource name
    */
   nres = (RES *)res->res_dir.hdr.next;
   if (res->res_dir.hdr.name) {
      free(res->res_dir.hdr.name);
   }
   if (res->res_dir.hdr.desc) {
      free(res->res_dir.hdr.desc);
   }


   switch (type) {
   case R_DIRECTOR:
      if (res->res_dir.password) {
         free(res->res_dir.password);
      }
      if (res->res_dir.address) {
         free(res->res_dir.address);
      }
      if (res->res_dir.tls_ctx) {
         free_tls_context(res->res_dir.tls_ctx);
      }
      if (res->res_dir.tls_ca_certfile) {
         free(res->res_dir.tls_ca_certfile);
      }
      if (res->res_dir.tls_ca_certdir) {
         free(res->res_dir.tls_ca_certdir);
      }
      if (res->res_dir.tls_certfile) {
         free(res->res_dir.tls_certfile);
      }
      if (res->res_dir.tls_keyfile) {
         free(res->res_dir.tls_keyfile);
      }
      if (res->res_dir.tls_dhfile) {
         free(res->res_dir.tls_dhfile);
      }
      if (res->res_dir.tls_allowed_cns) {
         delete res->res_dir.tls_allowed_cns;
      }
      if (res->res_dir.keyencrkey) {
         free(res->res_dir.keyencrkey);
      }
      break;
   case R_NDMP:
      if (res->res_ndmp.username) {
         free(res->res_ndmp.username);
      }
      if (res->res_ndmp.password) {
         free(res->res_ndmp.password);
      }
      break;
   case R_AUTOCHANGER:
      if (res->res_changer.changer_name) {
         free(res->res_changer.changer_name);
      }
      if (res->res_changer.changer_command) {
         free(res->res_changer.changer_command);
      }
      if (res->res_changer.device) {
         delete res->res_changer.device;
      }
      rwl_destroy(&res->res_changer.changer_lock);
      break;
   case R_STORAGE:
      if (res->res_store.sdaddrs) {
         free_addresses(res->res_store.sdaddrs);
      }
      if (res->res_store.working_directory) {
         free(res->res_store.working_directory);
      }
      if (res->res_store.pid_directory) {
         free(res->res_store.pid_directory);
      }
      if (res->res_store.subsys_directory) {
         free(res->res_store.subsys_directory);
      }
      if (res->res_store.plugin_directory) {
         free(res->res_store.plugin_directory);
      }
      if (res->res_store.scripts_directory) {
         free(res->res_store.scripts_directory);
      }
      if (res->res_store.tls_ctx) {
         free_tls_context(res->res_store.tls_ctx);
      }
      if (res->res_store.tls_ca_certfile) {
         free(res->res_store.tls_ca_certfile);
      }
      if (res->res_store.tls_ca_certdir) {
         free(res->res_store.tls_ca_certdir);
      }
      if (res->res_store.tls_certfile) {
         free(res->res_store.tls_certfile);
      }
      if (res->res_store.tls_keyfile) {
         free(res->res_store.tls_keyfile);
      }
      if (res->res_store.tls_dhfile) {
         free(res->res_store.tls_dhfile);
      }
      if (res->res_store.tls_allowed_cns) {
         delete res->res_store.tls_allowed_cns;
      }
      if (res->res_store.verid) {
         free(res->res_store.verid);
      }
      if (res->res_store.ndmpaddrs) {
         free_addresses(res->res_store.ndmpaddrs);
      }
      break;
   case R_DEVICE:
      if (res->res_dev.media_type) {
         free(res->res_dev.media_type);
      }
      if (res->res_dev.device_name) {
         free(res->res_dev.device_name);
      }
      if (res->res_dev.changer_name) {
         free(res->res_dev.changer_name);
      }
      if (res->res_dev.changer_command) {
         free(res->res_dev.changer_command);
      }
      if (res->res_dev.alert_command) {
         free(res->res_dev.alert_command);
      }
      if (res->res_dev.spool_directory) {
         free(res->res_dev.spool_directory);
      }
      if (res->res_dev.mount_point) {
         free(res->res_dev.mount_point);
      }
      if (res->res_dev.mount_command) {
         free(res->res_dev.mount_command);
      }
      if (res->res_dev.unmount_command) {
         free(res->res_dev.unmount_command);
      }
      if (res->res_dev.write_part_command) {
         free(res->res_dev.write_part_command);
      }
      if (res->res_dev.free_space_command) {
         free(res->res_dev.free_space_command);
      }
      break;
   case R_MSGS:
      if (res->res_msgs.mail_cmd) {
         free(res->res_msgs.mail_cmd);
      }
      if (res->res_msgs.operator_cmd) {
         free(res->res_msgs.operator_cmd);
      }
      free_msgs_res((MSGSRES *)res);  /* free message resource */
      res = NULL;
      break;
   default:
      Dmsg1(0, _("Unknown resource type %d\n"), type);
      break;
   }
   /*
    * Common stuff again -- free the resource, recurse to next one
    */
   if (res) {
      free(res);
   }
   if (nres) {
      free_resource(nres, type);
   }
}

/*
 * Save the new resource by chaining it into the head list for
 * the resource. If this is pass 2, we update any resource
 * or alist pointers.
 */
void save_resource(int type, RES_ITEM *items, int pass)
{
   URES *res;
   int rindex = type - r_first;
   int i, size;
   int error = 0;

   /*
    * Ensure that all required items are present
    */
   for (i = 0; items[i].name; i++) {
      if (items[i].flags & ITEM_REQUIRED) {
         if (!bit_is_set(i, res_all.res_dir.hdr.item_present)) {
            Emsg2(M_ERROR_TERM, 0, _("\"%s\" item is required in \"%s\" resource, but not found.\n"),
              items[i].name, resources[rindex]);
          }
      }
      /*
       * If this triggers, take a look at lib/parse_conf.h
       */
      if (i >= MAX_RES_ITEMS) {
         Emsg1(M_ERROR_TERM, 0, _("Too many items in \"%s\" resource\n"), resources[rindex]);
      }
   }

   /*
    * During pass 2, we looked up pointers to all the resources
    * referrenced in the current resource, , now we
    * must copy their address from the static record to the allocated
    * record.
    */
   if (pass == 2) {
      DEVRES *dev;
      int errstat;
      switch (type) {
      /*
       * Resources not containing a resource
       */
      case R_DEVICE:
      case R_MSGS:
      case R_NDMP:
         break;
      /*
       * Resources containing a resource or an alist
       */
      case R_DIRECTOR:
         if ((res = (URES *)GetResWithName(R_DIRECTOR, res_all.res_dir.hdr.name)) == NULL) {
            Emsg1(M_ERROR_TERM, 0, _("Cannot find Director resource %s\n"), res_all.res_dir.hdr.name);
         }
         res->res_dir.tls_allowed_cns = res_all.res_dir.tls_allowed_cns;
         break;
      case R_STORAGE:
         if ((res = (URES *)GetResWithName(R_STORAGE, res_all.res_dir.hdr.name)) == NULL) {
            Emsg1(M_ERROR_TERM, 0, _("Cannot find Storage resource %s\n"), res_all.res_dir.hdr.name);
         }
         res->res_store.messages = res_all.res_store.messages;
         res->res_store.tls_allowed_cns = res_all.res_store.tls_allowed_cns;
         break;
      case R_AUTOCHANGER:
         if ((res = (URES *)GetResWithName(type, res_all.res_changer.hdr.name)) == NULL) {
            Emsg1(M_ERROR_TERM, 0, _("Cannot find AutoChanger resource %s\n"),
                  res_all.res_changer.hdr.name);
         }
         /*
          * We must explicitly copy the device alist pointer
          */
         res->res_changer.device   = res_all.res_changer.device;
         /*
          * Now update each device in this resource to point back
          * to the changer resource.
          */
         foreach_alist(dev, res->res_changer.device) {
            dev->changer_res = (AUTOCHANGERRES *)&res->res_changer;
         }
         if ((errstat = rwl_init(&res->res_changer.changer_lock,
                                 PRIO_SD_ACH_ACCESS)) != 0)
         {
            berrno be;
            Jmsg1(NULL, M_ERROR_TERM, 0, _("Unable to init lock: ERR=%s\n"),
                  be.bstrerror(errstat));
         }
         break;
      default:
         printf(_("Unknown resource type %d\n"), type);
         error = 1;
         break;
      }


      if (res_all.res_dir.hdr.name) {
         free(res_all.res_dir.hdr.name);
         res_all.res_dir.hdr.name = NULL;
      }
      if (res_all.res_dir.hdr.desc) {
         free(res_all.res_dir.hdr.desc);
         res_all.res_dir.hdr.desc = NULL;
      }
      return;
   }

   /*
    * The following code is only executed on pass 1
    */
   switch (type) {
      case R_DIRECTOR:
         size = sizeof(DIRRES);
         break;
      case R_NDMP:
         size = sizeof(NDMPRES);
         break;
      case R_STORAGE:
         size = sizeof(STORES);
         break;
      case R_DEVICE:
         size = sizeof(DEVRES);
         break;
      case R_MSGS:
         size = sizeof(MSGSRES);
         break;
      case R_AUTOCHANGER:
         size = sizeof(AUTOCHANGERRES);
         break;
      default:
         printf(_("Unknown resource type %d\n"), type);
         error = 1;
         size = 1;
         break;
   }
   /*
    * Common
    */
   if (!error) {
      res = (URES *)malloc(size);
      memcpy(res, &res_all, size);
      if (!res_head[rindex]) {
         res_head[rindex] = (RES *)res; /* store first entry */
      } else {
         RES *next, *last;
         /*
          * Add new res to end of chain
          */
         for (last = next = res_head[rindex]; next; next = next->next) {
            last = next;
            if (bstrcmp(next->name, res->res_dir.hdr.name)) {
               Emsg2(M_ERROR_TERM, 0,
                  _("Attempt to define second \"%s\" resource named \"%s\" is not permitted.\n"),
                  resources[rindex].name, res->res_dir.hdr.name);
            }
         }
         last->next = (RES *)res;
         Dmsg2(90, "Inserting %s res: %s\n", res_to_str(type),
               res->res_dir.hdr.name);
      }
   }
}

/*
 * callback function for init_resource
 * See ../lib/parse_conf.c, function init_resource, for more generic handling.
 */
static void init_resource_cb(RES_ITEM *item)
{
   int i;

   if (item->handler == store_authtype) {
      for (i = 0; authmethods[i].name; i++) {
          if (bstrcasecmp(item->default_value, authmethods[i].name)) {
             *(uint32_t *)(item->value) = authmethods[i].token;
          }
      }
   }
}

bool parse_sd_config(CONFIG *config, const char *configfile, int exit_code)
{
   config->init(configfile, NULL, NULL, init_resource_cb, exit_code,
                (void *)&res_all, res_all_size, r_first,
                r_last, resources, res_head);
   return config->parse_config();
}
