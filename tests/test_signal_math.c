#include <assert.h>
#include <stdio.h>
#include "wtf_signal_math.h"

int main(void) {
    // Real source header: same code path firmware calls after receiving RSSI.
    assert(wtf_rssi_interval_ms(-120)==3000);
    assert(wtf_rssi_interval_ms(-95)==3000);
    assert(wtf_rssi_interval_ms(-90)==2600);
    assert(wtf_rssi_interval_ms(-80)==1800);
    assert(wtf_rssi_interval_ms(-70)==1100);
    assert(wtf_rssi_interval_ms(-60)==565);
    assert(wtf_rssi_interval_ms(-50)==263);
    assert(wtf_rssi_interval_ms(-46)==125);
    assert(wtf_rssi_interval_ms(-45)==0);
    assert(wtf_rssi_interval_ms(-25)==0);
    assert(wtf_led_display_rssi(-70,-45)==-70);
    assert(wtf_led_display_rssi(-95,-70)==-95);
    assert(wtf_led_display_rssi(-85,-70)==-75);
    assert(wtf_led_display_rssi(-70,-70)==-45);
    assert(wtf_led_display_rssi(-60,-70)==-45);
    assert(wtf_rssi_interval_ms(wtf_led_display_rssi(-70,-70))==0);
    int distinct = 0;
    for (int d=-95; d < -45; ++d) {
        const int a=wtf_rssi_interval_ms(d), b=wtf_rssi_interval_ms(d+1);
        assert(b >= 0 && b < a);
        if (a != b) ++distinct;
    }
    assert(distinct==50);
    assert(wtf_filter_rssi(-50,-90,0)==-60);
    assert(wtf_filter_rssi(-60,-35,0)==-35);
    assert(wtf_filter_rssi(-35,-75,1)==-75);
    int vote=0;
    vote=wtf_trend_vote(vote,-85,-80);
    assert(vote==1);
    vote=wtf_trend_vote(vote,-80,-75);
    assert(vote>=2);
    vote=wtf_trend_vote(vote,-75,-74); // 1dBm noise shouldn't change vote
    assert(vote>=2);
    vote=wtf_trend_vote(vote,-74,-80);
    assert(vote==-1);
    vote=wtf_trend_vote(vote,-80,-85);
    assert(vote<=-2);
    assert(wtf_strength_led_on(0,700,70,0));
    assert(!wtf_strength_led_on((uint32_t)(80*65536/700),700,70,0));
    assert(wtf_strength_led_on((uint32_t)(150*65536/700),700,70,1));
    assert(!wtf_strength_led_on((uint32_t)(150*65536/700),700,70,0));
    assert(wtf_strength_led_on(0,0,70,0)); // solid-on high RSSI
    uint32_t phase=wtf_phase_advance_q16(0,100,1000);
    assert(phase > 0 && phase < 65536);
    uint32_t phase2=wtf_phase_advance_q16(phase,100,500);
    assert(phase2 > phase);  // interval change preserves fractional phase
    assert(wtf_buzzer_interval_ms(3000,220)==3000);
    assert(wtf_buzzer_interval_ms(0,220)==220);
    assert(wtf_buzzer_interval_ms(125,220)==220);
    assert(wtf_buzzer_interval_ms(565,220)==565);
    assert(wtf_buzzer_chirp_on(0,220,60));
    assert(wtf_buzzer_chirp_on((uint32_t)(50*65536/220),220,60));
    assert(!wtf_buzzer_chirp_on((uint32_t)(100*65536/220),220,60));
    assert(!wtf_buzzer_chirp_on((uint32_t)(200*65536/220),220,60));
    puts("PASS: 50 distinct RSSI intervals, filter, trend, LED duty, phase continuity");
    return 0;
    //   Is there a better way to test this? 
}
