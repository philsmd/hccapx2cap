// Description: hashcat .hccapx file to .cap file conversion
// Release date: February 2017
//
// License: belongs to the PUBLIC DOMAIN, donated to hashcat, credits MUST go to hashcat
//          and philsmd for their hard work. Thx
// Disclaimer: WE PROVIDE THE PROGRAM “AS IS” WITHOUT WARRANTY OF ANY KIND, EITHER
//         EXPRESSED OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
//         OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//         Furthermore, NO GUARANTEES THAT IT WORKS FOR YOU AND WORKS CORRECTLY
//
// HOWTO compile: gcc -o hccapx2cap hccapx2cap.c
// HOWTO run:     ./hccapx2cap capture.hccapx capture_converted.cap
//
// Note: this is basically just an adaption of hccap2cap (see https://github.com/philsmd/hccap2cap/)
// to the new .hccapx format (see https://hashcat.net/wiki/hccapx)
// (it currently does not care about the message_pair value, even if it could generate the packets based on this value)

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CAP_MAGIC 0xA1B2C3D4;
#define CAP_VER_MAJOR 2
#define CAP_VER_MINOR 4
#define DEFAULT_SNAP_LEN 0xFFFF
#define IEEE802_11 105
#define DEFAULT_SEQUENCE "\x20\x50"
#define DEFAULT_BROADCAST_INTERVAL 0x64
#define DEFAULT_CAPABILITIES 0x0411 // 0b0000010000010001
//(immediate block ack,deplayed b.ack,dsss-ofdm,[UNUSED],power save,short slot time,1xcfp,
// spectrum mgmt,channel agility,pbcc,short preamble,privacy/web,2xcfp,ibss status,ess)
// FRAME CTRL: 2xversion,2xtype,4xsubtype,to,from,more,retry,power,data,wep,order == 16bits
#define DEFAULT_CHANNEL "\x01"
#define DEFAULT_TIM "\x00\x01\x00\x00"
#define DEFAULT_ERP "\x00"
#define DEFAULT_EXT_RATES "\x0c\x12\x18\x60" // 6 9 12 48
#define DEFAULT_RATES "\x82\x84\x8b\x96\x24\x30\x48\x6c" // 1 2 5.5 11 18 24 36 54
#define DEFAULT_VENDOR_TAG "\x00\x10\x18\x02\x00\xf0\x00\x00\x00" // OUI:e.g.BROADCOM(c),
// ... (3),type("\x02"),NOT interpretated
#define WMMWME_START_LEN 3
#define DEFAULT_WMMWME_END "\x02\x01\x01\x80\x00\x03\xa4\x00\x00\x27\xa4\x00\x00\x42\x43"\
    "\x5e\x00\x62\x32\x2f\x00"
// ...vendor typ,subtype,wme version,wme QoS,reserved,aci0,ecw,txop limit,aci1,ecw,
// txop limit,aci2,ecw,txop limit,aci3,ecw,txop limit
#define DEFAULT_WPAINFO_END "\x0c\x00"  // not totally sure why we need this
#define DEFAULT_HEADER_LEN 16
#define TAG_NUM_SSID 0
#define TAG_NUM_RATES 1
#define TAG_NUM_CHANNEL 3
#define TAG_NUM_TIM 5
#define TAG_NUM_ERP1 42                 // of course, whatelse could it be?
#define TAG_NUM_ERP2 47
#define TAG_NUM_EXT_RATES 50
#define TAG_NUM_VENDOR_SPEC 221
#define BROADCAST_FRAME_CTRL 0x0080     // 0b0000000010000000
#define REQUEST_FRAME_CTRL   0x0288     // 0b0000001010001000
#define REPLY_FRAME_CTRL     0x0188     // 0b0000000110001000
#define BSSID_BROADCAST "\xff\xff\xff\xff\xff\xff"
#define DEFAULT_DURATION 314            // 0x411
#define DEFAULT_DSAP '\xaa'             // SNAP - 170
#define DEFAULT_SSAP '\xaa'             // SNAP
#define DEFAULT_CTRL_FIELD 3            // unnumbered frame
#define DEFAULT_LINK_CTRL_TYPE 0x888e   // 802.1x authentication
#define DEFAULT_AUTH_LEN 0x5f           // 95
#define DEFAULT_KEY_INFO 0x89           // 0b0000000010001001, VERSION: RC4 HMAC-MD5 MIC
#define DEFAULT_KEY_LENGTH 32

// (encrypted key data,request,error,secure,mic,ack,install,2xindex,type,3xversion)
#define LSB (((union{unsigned x;unsigned char c;}){1}).c)   // check for little endian
#define SWAP(x) swap_bytes(&x,sizeof(x));

#define HCCAPX_SIGNATURE 0x58504348 // HCPX

typedef long long time64_t;

typedef struct
{
  unsigned int   signature;
  unsigned int   version;
  unsigned char  message_pair;
  unsigned char  essid_len;
  unsigned char  essid[32];
  unsigned char  keyver;
  unsigned char  keymic[16];
  unsigned char  mac_ap[6];
  unsigned char  nonce_ap[32];
  unsigned char  mac_sta[6];
  unsigned char  nonce_sta[32];
  unsigned short eapol_len;
  unsigned char  eapol[256];

} __attribute__((packed)) hccapx_struct;

typedef struct
{
  unsigned int   magic;
  unsigned short version_major;
  unsigned short version_minor;
  int            thiszone;
  unsigned int   sigfigs;
  unsigned int   snaplen;
  unsigned int   linktype;

} cap_header_struct;

typedef struct
{
  int tv_sec;
  int tv_usec;
  unsigned int len_cap;
  unsigned int len;
  short frame_ctrl;
  short duration;
  unsigned char dst[6];
  unsigned char src[6];
  unsigned char bssid[6];
  unsigned char seq_ctrl[2];  // sequence control (sequence number + fragment number)

} packet_header;

typedef struct tagged_node
{
  unsigned char  num;
  unsigned char  len;
  unsigned char *data;
  struct tagged_node *next;

} tagged_param;

typedef struct
{
  packet_header header;
  long timestamp;  // 64 bits
  short interval;
  short cap;       // capabilities
  tagged_param *tags;

} broadcast_struct;

typedef struct
{
  unsigned char tid;
  unsigned char qap_txop;
  unsigned char dsap;
  unsigned char ssap;
  unsigned char ctrl_field;
  unsigned char organization_code[3];
  unsigned short type;
  char version;
  char auth_type;
  short len;
  unsigned char descriptor_type;

} packet_info;

typedef struct
{
  short info;
  short len;
  unsigned char replay_counter[8];
  unsigned char nonce[32];    // supplicant nonce OR authenticator nonce
  unsigned char iv[16];
  unsigned char rsc[8];
  unsigned char id[8];
  unsigned char mic[16];
  short len_data;

} packet_info_key;

typedef struct
{
  packet_header    header;
  packet_info      info;
  packet_info_key  info_key;
  tagged_param    *data;

} packet_struct;

void usage (char *prog_name)
{
  printf ("Usage: %s <in.hccapx> <out.cap>\n", prog_name);
}

void swap_bytes (void *pv, size_t n)
{
  if (LSB)
  {
    char *ptr = pv, tmp;

    size_t low, high;

    for (low = 0, high = n - 1; high > low; low++, high--)
    {
      tmp = ptr[low];

      ptr[low] = ptr[high];
      ptr[high] = tmp;
    }
  }
}

int main (int argc, char **argv)
{
  FILE *hccapx_file, *cap_file;

  if (argc < 2)
  {
    fprintf (stderr, "[-] Please specify the input .hccapx file\n");

    usage (argv[0]);

    return 1;
  }

  hccapx_file = fopen (argv[1], "rb");

  if (hccapx_file == NULL)
  {
    fprintf (stderr, "[-] Could not open .hccapx file\n");

    return 1;
  }

  if (argc < 3)
  {
    fprintf (stderr, "[-] Please specify the output .cap file\n");

    usage (argv[0]);

    return 1;
  }

  cap_file = fopen (argv[2], "wb");

  if (cap_file == NULL)
  {
    fprintf (stderr, "[-] Could not open .cap file\n");

    return 1;
  }

  // do some validation of the input file (TODO: more validation needed)

  long size_hccapx_file;
  fseek (hccapx_file, 0, SEEK_END);

  size_hccapx_file = ftell (hccapx_file);

  if ((size_hccapx_file % sizeof (hccapx_struct)) != 0)
  {
    fprintf (stderr, "[-] .hccapx file seems to be *not* valid\n");

    return 1;
  }

  int hccapx_amount = size_hccapx_file / sizeof (hccapx_struct); // for multi hccapx support

  if (hccapx_amount < 1)
  {
    fprintf (stderr, "[-] .hccapx file seems to be empty\n");

    return 1;
  }

  // cap HEADER

  cap_header_struct cap_hdr;
  memset (&cap_hdr, 0, sizeof (cap_hdr));

  cap_hdr.magic         = CAP_MAGIC;
  cap_hdr.version_major = CAP_VER_MAJOR;
  cap_hdr.version_minor = CAP_VER_MINOR;
  cap_hdr.snaplen       = DEFAULT_SNAP_LEN;
  cap_hdr.linktype      = IEEE802_11;

  // write the cap header to file

  if (fwrite (&cap_hdr, sizeof (cap_hdr), 1, cap_file) == 0)
  {
    fprintf (stderr, "[-] Failed to write to .cap file\n");

    return 1;
  }

  rewind (hccapx_file); // seek to beginning of .hccapx file

  while (hccapx_amount--)
  {
    // copy the .hccapx "file parts" to the hccapx_struct
    // could be dangerous w/o more validation

    hccapx_struct hccapx;

    memset (&hccapx, 0, sizeof (hccapx));

    fread (&hccapx, 1, sizeof (hccapx), hccapx_file);

    if (hccapx.signature != HCCAPX_SIGNATURE)
    {
      fprintf (stderr, "[-] Failed to validate the signature (magic) of the .hccapx structure. SKIPPED\n");

      continue;
    }

    // here you can print/debug those values if you want, e.g.
    // printf ("keyver: %i\n", hccapx.keyver);

    // BROADCAST PACKET
    // the broadcast packet

    int current_time = time (NULL);

    broadcast_struct packet_broadcast;
    memset (&packet_broadcast, 0, sizeof (packet_broadcast));

    packet_broadcast.timestamp = ((unsigned long) current_time) * 1000;
    packet_broadcast.interval  = DEFAULT_BROADCAST_INTERVAL;
    packet_broadcast.cap       = DEFAULT_CAPABILITIES;

    // broadcast packet - tag list

    unsigned char vendor_data[hccapx.eapol[100]]; // another TODO: make those indices

    // ... in hccapx.eapol either as CONSTANT (#define) or depend on some lengths!
    memcpy (vendor_data, hccapx.eapol + 101, hccapx.eapol[100]);

    unsigned char wmmwme[WMMWME_START_LEN + sizeof (DEFAULT_WMMWME_END)];

    memcpy (wmmwme, vendor_data, WMMWME_START_LEN);
    memcpy (wmmwme + WMMWME_START_LEN, DEFAULT_WMMWME_END, sizeof (DEFAULT_WMMWME_END) - 1);

    tagged_param tag_wmmwme = {TAG_NUM_VENDOR_SPEC, (sizeof (wmmwme) - 1) / sizeof (char), wmmwme, NULL};

    // we need to allocate some memory first
    unsigned char wpa_info[hccapx.eapol[100] + sizeof (DEFAULT_WPAINFO_END)];

    memcpy (wpa_info, vendor_data, hccapx.eapol[100]);
    memcpy (wpa_info + hccapx.eapol[100], DEFAULT_WPAINFO_END, sizeof (DEFAULT_WPAINFO_END) - 1);

    tagged_param tag_wpainfo   = {TAG_NUM_VENDOR_SPEC, sizeof (wpa_info) - 1 / sizeof (char), wpa_info, &tag_wmmwme};
    tagged_param tag_vendor    = {TAG_NUM_VENDOR_SPEC, (sizeof (DEFAULT_VENDOR_TAG) - 1) / sizeof (char), DEFAULT_VENDOR_TAG, &tag_wpainfo};
    tagged_param tag_ext_rates = {TAG_NUM_EXT_RATES, (sizeof (DEFAULT_EXT_RATES) - 1) / sizeof (char), DEFAULT_EXT_RATES, &tag_vendor};
    tagged_param tag_erp2      = {TAG_NUM_ERP2, 1, DEFAULT_ERP, &tag_ext_rates};
    tagged_param tag_erp1      = {TAG_NUM_ERP1, 1, DEFAULT_ERP, &tag_erp2};
    tagged_param tag_tim       = {TAG_NUM_TIM, (sizeof (DEFAULT_TIM) - 1) / sizeof (char), DEFAULT_TIM, &tag_erp1};

    // ... traffic indication map

    tagged_param tag_cur_channel = {TAG_NUM_CHANNEL, 1, DEFAULT_CHANNEL, &tag_tim};
    tagged_param tag_rates       = {TAG_NUM_RATES, (sizeof (DEFAULT_RATES) - 1) / sizeof (char), DEFAULT_RATES, &tag_cur_channel};
    tagged_param tag_ssid        = {TAG_NUM_SSID, strlen (hccapx.essid), hccapx.essid, &tag_rates};
    packet_broadcast.tags        = &tag_ssid;
    tagged_param *tag_pointer    = packet_broadcast.tags;

    // its header

    int msg_size = sizeof (packet_broadcast.header)   + sizeof (packet_broadcast.timestamp) +
                   sizeof (packet_broadcast.interval) + sizeof (packet_broadcast.cap) - DEFAULT_HEADER_LEN;

    while (tag_pointer != NULL)
    {
      msg_size    += sizeof (tag_pointer->num) + sizeof (tag_pointer->len) + tag_pointer->len;
      tag_pointer  = tag_pointer->next;
    }

    tag_pointer = packet_broadcast.tags;  // reset to original root tag

    packet_header packet_broadcast_hdr;
    memset (&packet_broadcast_hdr, 0, sizeof (packet_broadcast_hdr));

    packet_broadcast_hdr.tv_sec     = current_time;
    packet_broadcast_hdr.len_cap    = packet_broadcast_hdr.len = msg_size;
    packet_broadcast_hdr.frame_ctrl = BROADCAST_FRAME_CTRL;

    memcpy (&packet_broadcast_hdr.dst,      BSSID_BROADCAST,  sizeof (packet_broadcast_hdr.dst));
    memcpy (&packet_broadcast_hdr.src,      hccapx.mac_ap,    sizeof (packet_broadcast_hdr.src));
    memcpy (&packet_broadcast_hdr.bssid,    hccapx.mac_ap,    sizeof (packet_broadcast_hdr.bssid));
    memcpy (&packet_broadcast_hdr.seq_ctrl, DEFAULT_SEQUENCE, sizeof (packet_broadcast_hdr.seq_ctrl));

    // assign header to broadcast message

    packet_broadcast.header = packet_broadcast_hdr;

    // OUTPUT the broadcast packet header

    if (fwrite (&packet_broadcast.header,    sizeof (packet_broadcast.header),    1, cap_file) == 0 ||
        fwrite (&packet_broadcast.timestamp, sizeof (packet_broadcast.timestamp), 1, cap_file) == 0 ||
        fwrite (&packet_broadcast.interval,  sizeof (packet_broadcast.interval),  1, cap_file) == 0 ||
        fwrite (&packet_broadcast.cap,       sizeof (packet_broadcast.cap),       1, cap_file) == 0 )
    {
      fprintf (stderr, "[-] ERROR while writing broadcast header to .cap file\n");

      return 1;
    }

    // OUTPUT tagged parameters of the broadcast packet

    while (tag_pointer != NULL)
    {
      if (fwrite (&tag_pointer->num, sizeof (tag_pointer->num), 1, cap_file) == 0 ||
          fwrite (&tag_pointer->len, sizeof (tag_pointer->len), 1, cap_file) == 0 ||
          fwrite (tag_pointer->data,         tag_pointer->len,  1, cap_file) == 0 )
      {
        fprintf (stderr, "[-] ERROR while writing broadcast tags to .cap file\n");

        return 1;
      }

      tag_pointer = tag_pointer->next;
    }

    // REQUEST
    // packet info

    packet_info packet_request_info;
    memset (&packet_request_info, 0, sizeof (packet_request_info));

    packet_request_info.dsap       = DEFAULT_DSAP;
    packet_request_info.ssap       = DEFAULT_SSAP;
    packet_request_info.ctrl_field = DEFAULT_CTRL_FIELD;
    packet_request_info.type       = DEFAULT_LINK_CTRL_TYPE;

    SWAP (packet_request_info.type);

    packet_request_info.version   = hccapx.keyver;
    packet_request_info.auth_type = hccapx.eapol[1];
    packet_request_info.len       = DEFAULT_AUTH_LEN;

    SWAP (packet_request_info.len);

    packet_request_info.descriptor_type = hccapx.eapol[4];

    // packet info key

    packet_info_key packet_request_info_key;
    memset (&packet_request_info_key, 0, sizeof (packet_request_info_key));

    packet_request_info_key.info = DEFAULT_KEY_INFO;
    SWAP (packet_request_info_key.info);

    packet_request_info_key.len = DEFAULT_KEY_LENGTH;
    SWAP (packet_request_info_key.len);

    memcpy (&packet_request_info_key.replay_counter, hccapx.eapol + 9, sizeof (packet_request_info_key.replay_counter));
    memcpy (&packet_request_info_key.nonce,          hccapx.nonce_ap,  sizeof (packet_request_info_key.nonce));

    // ... set ANONCE (...rest of request is zero'ed)
    // the broadcast packet

    packet_struct packet_request;
    memset (&packet_request, 0, sizeof (packet_request));

    packet_request.info = packet_request_info;
    packet_request.info_key = packet_request_info_key;

    // its header

    packet_header packet_request_hdr;
    memset (&packet_request_hdr, 0, sizeof (packet_request_hdr));

    packet_request_hdr.tv_sec     = current_time;
    packet_request_hdr.frame_ctrl = REQUEST_FRAME_CTRL;
    packet_request_hdr.duration   = DEFAULT_DURATION;

    memcpy (&packet_request_hdr.dst,   hccapx.mac_sta, sizeof (packet_request_hdr.dst));
    memcpy (&packet_request_hdr.src,   hccapx.mac_ap,  sizeof (packet_request_hdr.src));
    memcpy (&packet_request_hdr.bssid, hccapx.mac_ap,  sizeof (packet_request_hdr.bssid));

    msg_size = sizeof (packet_request_hdr) + (sizeof (packet_request.info) - 1) +
               sizeof (packet_request.info_key) - DEFAULT_HEADER_LEN;

    packet_request_hdr.len_cap = packet_request_hdr.len = msg_size;

    // assign header to request message
    packet_request.header = packet_request_hdr;

    // OUTPUT the full REQUEST

    if (fwrite (&packet_request.header,   sizeof (packet_request.header),   1, cap_file) == 0 ||
        fwrite (&packet_request.info,     sizeof (packet_request.info) - 1, 1, cap_file) == 0 ||
        fwrite (&packet_request.info_key, sizeof (packet_request.info_key), 1, cap_file) == 0 )
    {
      fprintf (stderr, "[-] ERROR while writing request to .cap file\n");

      return 1;
    }

    // REPLY
    // packet info

    packet_info packet_reply_info;
    memset (&packet_reply_info, 0, sizeof (packet_reply_info));

    packet_reply_info.dsap       = DEFAULT_DSAP;
    packet_reply_info.ssap       = DEFAULT_SSAP;
    packet_reply_info.ctrl_field = DEFAULT_CTRL_FIELD;
    packet_reply_info.type       = DEFAULT_LINK_CTRL_TYPE;

    SWAP (packet_reply_info.type);

    packet_reply_info.version   = hccapx.eapol[0];
    packet_reply_info.auth_type = hccapx.eapol[1];
    packet_reply_info.len       = hccapx.eapol[3] + (hccapx.eapol[2] << 8);

    SWAP (packet_reply_info.len);

    packet_reply_info.descriptor_type = hccapx.eapol[4];

    // packet info key

    packet_info_key packet_reply_info_key;

    memset (&packet_reply_info_key, 0, sizeof (packet_reply_info_key));

    packet_reply_info_key.info = hccapx.eapol[6] + (hccapx.eapol[5] << 8);

    SWAP (packet_reply_info_key.info);

    packet_reply_info_key.len = hccapx.eapol[8] + (hccapx.eapol[7] << 8);
    // ... note: this value ^ should be same as DEFAULT_KEY_LENGTH

    SWAP (packet_reply_info_key.len);

    memcpy (&packet_reply_info_key.replay_counter, hccapx.eapol + 9,
           sizeof (packet_reply_info_key.replay_counter));

    memcpy (&packet_reply_info_key.nonce, hccapx.nonce_sta, sizeof (packet_reply_info_key.nonce));

    // ... set SNONCE
    memcpy (&packet_reply_info_key.mic, hccapx.keymic, sizeof (packet_reply_info_key.mic));

    packet_reply_info_key.len_data = hccapx.eapol[98] + (hccapx.eapol[97] << 8);

    SWAP (packet_reply_info_key.len_data);

    // maybe we need (sometimes) to set also following fields: iv, rsc, id TODO

    // packet data

    tagged_param packet_reply_data = {hccapx.eapol[99], hccapx.eapol[100], vendor_data, NULL};

    // the broadcast packet

    packet_struct packet_reply;

    memset (&packet_reply, 0, sizeof (packet_reply));

    packet_reply.info     = packet_reply_info;
    packet_reply.info_key = packet_reply_info_key;
    packet_reply.data     = &packet_reply_data;

    // its header

    packet_header packet_reply_hdr;

    memset (&packet_reply_hdr, 0, sizeof (packet_reply_hdr));

    msg_size = sizeof (packet_reply.header)    + (sizeof (packet_reply.info) - 1) +
               sizeof (packet_reply.info_key)  + sizeof (packet_reply.data->num)  +
               sizeof (packet_reply.data->len) + packet_reply.data->len - DEFAULT_HEADER_LEN;

    packet_reply_hdr.tv_sec     = current_time;
    packet_reply_hdr.len_cap    = packet_reply_hdr.len = msg_size;
    packet_reply_hdr.frame_ctrl = REPLY_FRAME_CTRL;
    packet_reply_hdr.duration   = DEFAULT_DURATION;

    memcpy (&packet_reply_hdr.dst,   hccapx.mac_ap,  sizeof (packet_reply_hdr.dst));
    memcpy (&packet_reply_hdr.src,   hccapx.mac_sta, sizeof (packet_reply_hdr.src));
    memcpy (&packet_reply_hdr.bssid, hccapx.mac_ap,  sizeof (packet_reply_hdr.bssid));

    // assign header to reply message

    packet_reply.header = packet_reply_hdr;

    // OUTPUT the full REPLY

    if (fwrite (&packet_reply.header,      sizeof (packet_reply.header),    1, cap_file) == 0 ||
        fwrite (&packet_reply.info,        sizeof (packet_reply.info) - 1,  1, cap_file) == 0 ||
        fwrite (&packet_reply.info_key,    sizeof (packet_reply.info_key),  1, cap_file) == 0 ||
        fwrite (&(packet_reply.data)->num, sizeof (packet_reply.data->num), 1, cap_file) == 0 ||
        fwrite (&(packet_reply.data)->len, sizeof (packet_reply.data->len), 1, cap_file) == 0 ||
        fwrite ((packet_reply.data)->data, packet_reply.data->len,          1, cap_file) == 0 )
    {
      fprintf (stderr, "[-] ERROR while writing reply to .cap file\n");

      return 1;
    }
  }

  fclose (cap_file);
  fclose (hccapx_file);

  return 0;
}
