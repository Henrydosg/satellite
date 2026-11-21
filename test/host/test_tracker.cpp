#include "../../src/tle_validation.h"
#include "../../lib/Sgp4/src/Sgp4.h"
#include <assert.h>
#include <stdio.h>
#include <string>

// Frozen CelesTrak ISS fixture; never linked into the device firmware.
static const char* L1 = "1 25544U 98067A   26324.88180268  .00009897  00000+0  18414-3 0  9991";
static const char* L2 = "2 25544  51.6319 244.6605 0007389  48.0812 312.0806 15.49518511596048";
static std::string checksum(std::string line) {
    unsigned sum = 0;
    for (size_t i = 0; i < 68; ++i) {
        if (line[i] >= '0' && line[i] <= '9') sum += line[i] - '0';
        else if (line[i] == '-') ++sum;
    }
    line[68] = '0' + sum % 10;
    return line;
}
int main() {
    using namespace TLEValidation;
    assert(pairValid(25544, L1, L2));
    assert(!pairValid(25338, L1, L2));
    assert(!pairValid(25544, "", L2));
    assert(!pairValid(25544, std::string(L1).substr(0, 68).c_str(), L2));
    std::string damaged = L2;
    damaged[9] = damaged[9] == '9' ? '0' : '9'; // Change a digit, so checksum must change.
    assert(!pairValid(25544, L1, damaged.c_str()));

    std::string epoch = L1;
    epoch.replace(18, 14, "24001.00000000");
    assert(epochFromLine1(epoch.c_str()) == (time_t)1704067200);
    _putenv_s("TZ", "UTC-7"); _tzset();
    assert(epochFromLine1(epoch.c_str()) == (time_t)1704067200);
    _putenv_s("TZ", "UTC+8"); _tzset();
    assert(epochFromLine1(epoch.c_str()) == (time_t)1704067200);
    epoch.replace(18, 14, "24366.00000000");
    assert(epochFromLine1(epoch.c_str()) == (time_t)1735603200);
    epoch.replace(18, 14, "25366.00000000");
    assert(epochFromLine1(epoch.c_str()) == 0);
    assert(!timeReady(0) && timeReady(1704067200));
    assert(!observerValid(false, 0, 0));
    assert(observerValid(true, 0, 0)); // Real user-confirmed 0,0 must remain valid.
    assert(!observerValid(true, NAN, 0) && !observerValid(true, 91, 0));

    Sgp4 predictor;
    char line1[130], line2[130];
    snprintf(line1, sizeof(line1), "%s", L1);
    snprintf(line2, sizeof(line2), "%s", L2);
    assert(predictor.init("ISS", line1, line2));
    assert(strcmp(line1, L1) == 0 && strcmp(line2, L2) == 0);
    predictor.site(47.0, 8.0, 0.0);
    assert(predictor.init("ISS", line1, line2));
    predictor.findsat(predictor.satrec.jdsatepoch);
    assert(predictor.satrec.error == 0);
    assert(isfinite(predictor.satLat) && isfinite(predictor.satLon));
    assert(predictor.satAlt > 200 && predictor.satAlt < 1000);
    double speed = sqrt(predictor.vo[0]*predictor.vo[0] + predictor.vo[1]*predictor.vo[1] + predictor.vo[2]*predictor.vo[2]);
    assert(speed > 6 && speed < 9);
    assert(predictor.initpredpoint(predictor.satrec.jdsatepoch, 0.0));
    passinfo pass = {};
    assert(predictor.nextpass(&pass, 20));
    assert(pass.jdstart < pass.jdmax && pass.jdmax < pass.jdstop);
    assert(isfinite(pass.maxelevation) && pass.maxelevation > 0 && pass.maxelevation <= 90);
    std::string changed = L2;
    changed.replace(8, 8, " 50.6319");
    changed = checksum(changed);
    snprintf(line2, sizeof(line2), "%s", changed.c_str());
    assert(predictor.init("ISS", line1, line2));
    assert(strcmp(predictor.line2, changed.c_str()) == 0);
    assert(fabs(predictor.satrec.inclo * 180.0 / pi - 50.6319) < 1e-5);
    puts("PASS: TLE length/ID/checksum, UTC epoch/timezone/leap year, readiness, observer validity, SGP4 repeat init/input preservation/changed line2 and finite orbital telemetry");
}
