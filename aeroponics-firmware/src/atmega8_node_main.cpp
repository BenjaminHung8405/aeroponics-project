#if defined(__AVR__)
#include "rf_frame_codec.h"
#include "config.h"
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <cstring>

namespace {
volatile uint32_t g_now_ms = 0;
ISR(TIMER1_COMPA_vect) { ++g_now_ms; }
uint32_t nowMs() { uint8_t s = SREG; uint32_t n; cli(); n = g_now_ms; SREG = s; return n; }
void initClock() { TCCR1A = 0; TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10); OCR1A = 249; TIMSK |= _BV(OCIE1A); }

#ifndef AGU_NODE_ID
#define AGU_NODE_ID 4
#endif
constexpr uint8_t NODE_ID = static_cast<uint8_t>(AGU_NODE_ID);
static_assert(NODE_ID >= AGU_LEGACY_MIN_NODE_ID && NODE_ID <= AGU_LEGACY_MAX_NODE_ID,
              "ATmega8 AGU node ID must be one of physical RF IDs 4..7");
constexpr uint16_t PSK_EEPROM = 256;
constexpr uint16_t SCHEDULE_EEPROM = 0;
constexpr uint16_t MAGIC = 0xA85A;
struct ScheduleRecord { uint16_t magic; uint8_t version; uint8_t node; uint32_t spray; uint32_t cooldown; uint8_t enabled; uint8_t checksum; };
uint8_t recordChecksum(const ScheduleRecord& r) { const uint8_t* b = reinterpret_cast<const uint8_t*>(&r); uint8_t x=0; for (uint8_t i=0;i<sizeof(r)-1;++i) x^=b[i]; return x; }

uint8_t g_psk[16], g_rx[RF_MAX_RX_BUFFER_SIZE], g_tx[RF_MAX_FRAME_SIZE];
uint8_t g_pump = 0, g_fault = 0, g_schedule = 0, g_phase = 0, g_override = 0;
uint32_t g_spray = 30000, g_cooldown = 600000, g_phase_start = 0, g_override_start = 0, g_override_duration = 0;
uint32_t g_lease_start = 0, g_lease_duration = 0, g_max_on = 0, g_command = 0;
uint32_t g_gateway_session = 0, g_cached_session = 0, g_cached_command = 0;
uint16_t g_last_sequence = 0, g_cached_sequence = 0;
uint8_t g_session_valid = 0, g_cached_valid = 0; CommandAckPayload g_cached_ack{};

void pump(uint8_t on) { if (on) PORTD |= _BV(PORTD4); else PORTD &= static_cast<uint8_t>(~_BV(PORTD4)); g_pump=on; }
void uartBegin() { UBRRH=0; UBRRL=103; UCSRB=_BV(RXEN)|_BV(TXEN); UCSRC=_BV(URSEL)|_BV(UCSZ1)|_BV(UCSZ0); }
size_t uartReceive(uint8_t* b, size_t n) { size_t i=0; while ((UCSRA&_BV(RXC)) && i<n) b[i++]=UDR; return i; }
size_t uartSend(const uint8_t* b, size_t n) { if (!b) return 0; for(size_t i=0;i<n;++i) { while(!(UCSRA&_BV(UDRE))) {} UDR=b[i]; } return n; }
void sendAck(const RfHeader& h, uint8_t outcome) {
    g_cached_ack={h.sequence,outcome,g_pump,g_pump,{0,0,0}}; g_cached_session=h.boot_session_id; g_cached_sequence=h.sequence; g_cached_command=h.command_id; g_cached_valid=1;
    RfFrameMetadata m(NODE_ID,RF_GATEWAY_NODE_ID,1,1,h.command_id); size_t n=RfFrameCodec::encodeFrame(m,RfMessageType::COMMAND_ACK,&g_cached_ack,sizeof(g_cached_ack),g_psk,sizeof(g_psk),g_tx,sizeof(g_tx)); if(n) uartSend(g_tx,n);
}
void loadSchedule() { ScheduleRecord r{}; eeprom_read_block(&r,reinterpret_cast<const void*>(SCHEDULE_EEPROM),sizeof(r)); if(r.magic==MAGIC&&r.version==1&&r.node==NODE_ID&&r.checksum==recordChecksum(r)&&r.spray&&r.cooldown&&r.enabled<=1) {g_spray=r.spray;g_cooldown=r.cooldown;g_schedule=r.enabled;} }
void service(uint32_t now) {
    if(g_fault) { pump(0); g_override=0; return; }
    if(g_override==2 && (now-g_lease_start>=g_lease_duration || now-g_lease_start>=g_max_on)) { pump(0); g_fault=3; g_override=0; return; }
    if(g_override==1 && now-g_override_start>=g_override_duration) { g_override=0; g_phase=0; g_phase_start=now; }
    if(g_override==0 && g_schedule) { uint32_t elapsed=now-g_phase_start; if(g_phase==0) {pump(0); if(elapsed>=g_cooldown){g_phase=1;g_phase_start=now;pump(1);}} else {pump(1); if(elapsed>=g_spray){g_phase=0;g_phase_start=now;pump(0);}} }
    else if(!g_schedule && g_override==0) pump(0);
}
void process(const uint8_t* data, size_t len, uint32_t now) {
    RfHeader h{}; uint8_t payload[RF_MAX_PAYLOAD_SIZE]{}; if(!RfFrameCodec::decodeFrame(data,len,g_psk,sizeof(g_psk),h,payload,sizeof(payload))) return;
    if(h.target_node_id!=NODE_ID || h.source_node_id!=RF_GATEWAY_NODE_ID) return;
    if(!g_session_valid || h.boot_session_id>g_gateway_session) {g_gateway_session=h.boot_session_id;g_last_sequence=h.sequence;g_session_valid=1;g_cached_valid=0;}
    else if(h.boot_session_id<g_gateway_session) return;
    else if(g_cached_valid&&h.sequence==g_cached_sequence&&h.command_id==g_cached_command) { RfFrameMetadata m(NODE_ID,0,1,1,h.command_id); size_t n=RfFrameCodec::encodeFrame(m,RfMessageType::COMMAND_ACK,&g_cached_ack,sizeof(g_cached_ack),g_psk,16,g_tx,sizeof(g_tx));if(n)uartSend(g_tx,n);return; }
    else if(!RfFrameCodec::isSequenceAdvanceValid(h.sequence,g_last_sequence)) return; else g_last_sequence=h.sequence;
    if(static_cast<RfMessageType>(h.message_type)==RfMessageType::PING) { PingPayload ping{}; RfFrameCodec::decodePayload(RfMessageType::PING,payload,h.payload_len,&ping,sizeof(ping)); PongPayload p{ping.ping_timestamp_ms}; RfFrameMetadata m(NODE_ID,0,1,1,0);size_t n=RfFrameCodec::encodeFrame(m,RfMessageType::PONG,&p,sizeof(p),g_psk,16,g_tx,sizeof(g_tx));if(n)uartSend(g_tx,n);return; }
    if(static_cast<RfMessageType>(h.message_type)!=RfMessageType::SET_PUMP) return;
    SetPumpPayload p{}; if(!RfFrameCodec::decodePayload(RfMessageType::SET_PUMP,payload,h.payload_len,&p,sizeof(p))) return;
    if(p.desired_state>1 || (p.desired_state && (!p.run_lease_ms||p.max_on_duration_ms<p.run_lease_ms))) {sendAck(h,static_cast<uint8_t>(AckOutcome::REJECTED_INVALID_LEASE));return;}
    if(p.desired_state) { pump(1);g_override=2;g_lease_start=now;g_lease_duration=p.run_lease_ms;g_max_on=p.max_on_duration_ms;g_command=h.command_id; }
    else {pump(0);g_override=1;g_override_start=now;g_override_duration=p.run_lease_ms?p.run_lease_ms:g_spray;g_command=h.command_id;}
    sendAck(h,static_cast<uint8_t>(AckOutcome::SUCCESS));
}
}

int main() {
    PORTD &= static_cast<uint8_t>(~_BV(PORTD4)); DDRD |= _BV(DDD4); pump(0);
    eeprom_read_block(g_psk,reinterpret_cast<const void*>(PSK_EEPROM),sizeof(g_psk)); loadSchedule(); initClock(); uartBegin(); sei();
    for(;;) { uint32_t n=nowMs(); size_t len=uartReceive(g_rx,sizeof(g_rx)); if(len) process(g_rx,len,n); service(n); }
}
#endif
