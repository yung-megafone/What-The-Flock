#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include "wtf_scan_math.h"
int main(void) {
    const int expected_p2[] = {1,6,11,1,6,11,2,1,6,11,1,6,11,3};
    for (unsigned i=0;i<14;i++) {
        assert(wtf_scan_next_channel(2,i)==expected_p2[i]);
    }
    for (unsigned i=0;i<33;i++) {
        const unsigned ch = wtf_scan_next_channel(1,i);
        assert(ch==1||ch==6||ch==11);
    }
    for (unsigned i=0;i<26;i++) assert(wtf_scan_next_channel(3,i)==1+i%13);
    bool seen[14]={0};
    for (unsigned i=0;i<70;i++) seen[wtf_scan_next_channel(2,i)]=true;
    for (unsigned i=1;i<=13;i++) assert(seen[i]);
    assert(wtf_scan_dwell_ms(2,6)==125);
    assert(wtf_scan_lock_valid(1500,1300,1000));
    assert(!wtf_scan_lock_valid(2800,1000,1000));
    assert(!wtf_scan_lock_valid(5500,5400,1000));
    assert(!wtf_scan_candidate_ready(1,1,1000,1000));
    assert(wtf_scan_candidate_ready(2,1,1000,1000));
    assert(wtf_scan_candidate_ready(1,2,1500,1000));
    assert(!wtf_scan_candidate_ready(1,2,2400,1000));
    puts("PASS: scan profiles, secondary coverage, candidate confidence, lock expiry");
}
