#include "reader_util.h"
#include "ndpi_api.h"

#include <pcap/pcap.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

struct ndpi_workflow_prefs *prefs = NULL;

int nDPI_LogLevel = 0;
char *_debug_protocols = NULL;
u_int32_t current_ndpi_memory = 0, max_ndpi_memory = 0;
u_int8_t enable_protocol_guess = 1, enable_payload_analyzer = 0;
u_int8_t enable_joy_stats = 0;
u_int8_t human_readeable_string_len = 5;
u_int8_t max_num_udp_dissected_pkts = 16 /* 8 is enough for most protocols, Signal requires more */, max_num_tcp_dissected_pkts = 80 /* due to telnet */;

int bufferToFile(const char * name, const uint8_t *Data, size_t Size) {
  FILE * fd;
  if (remove(name) != 0) {
    if (errno != ENOENT) {
      printf("failed remove, errno=%d\n", errno);
      return -1;
    }
  }
  fd = fopen(name, "wb");
  if (fd == NULL) {
    printf("failed open, errno=%d\n", errno);
    return -2;
  }
  if (fwrite (Data, 1, Size, fd) != Size) {
    fclose(fd);
    return -3;
  }
  fclose(fd);
  return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  pcap_t * pkts;
  const u_char *pkt;
  struct pcap_pkthdr *header;
  int r;
  char errbuf[PCAP_ERRBUF_SIZE];
  NDPI_PROTOCOL_BITMASK all;

  if (prefs == NULL) {
    prefs = calloc(sizeof(struct ndpi_workflow_prefs), 1);
    if (prefs == NULL) {
      //should not happen
      return 1;
    }
    prefs->decode_tunnels = 1;
    prefs->num_roots = 16;
    prefs->max_ndpi_flows = 1024;
    prefs->quiet_mode = 0;
  }
  bufferToFile("/tmp/fuzz.pcap", Data, Size);

  pkts = pcap_open_offline("/tmp/fuzz.pcap", errbuf);
  if (pkts == NULL) {
    return 0;
  }
  struct ndpi_workflow * workflow = ndpi_workflow_init(prefs, pkts);
  // enable all protocols
  NDPI_BITMASK_SET_ALL(all);
  ndpi_set_protocol_detection_bitmask2(workflow->ndpi_struct, &all);
  memset(workflow->stats.protocol_counter, 0,
	 sizeof(workflow->stats.protocol_counter));
  memset(workflow->stats.protocol_counter_bytes, 0,
	 sizeof(workflow->stats.protocol_counter_bytes));
  memset(workflow->stats.protocol_flows, 0,
	 sizeof(workflow->stats.protocol_flows));
  ndpi_finalize_initalization(workflow->ndpi_struct);

  r = pcap_next_ex(pkts, &header, &pkt);
  while (r > 0) {
    /* allocate an exact size buffer to check overflows */
    uint8_t *packet_checked = malloc(header->caplen);
    memcpy(packet_checked, pkt, header->caplen);
    ndpi_workflow_process_packet(workflow, header, packet_checked);
    free(packet_checked);
    r = pcap_next_ex(pkts, &header, &pkt);
  }
  ndpi_workflow_free(workflow);
  pcap_close(pkts);

  return 0;
}
