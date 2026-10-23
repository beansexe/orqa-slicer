#include "printing/FastStartWarning.hpp"
#include <limits>
#include <stdexcept>
#include <iostream>
using namespace Slic3r::VendorPrinting;
int main() {
    int checks = 0;
    auto check = [&](bool ok) { if (!ok) throw std::runtime_error("Fast Start policy failed"); ++checks; };
    check(!is_fast_start("Bambu Lab X1 Carbon 0.4 nozzle", "G28\nM109 S200"));
    check(is_fast_start("Bambu Lab X1 Carbon - Fast Start", ""));
    check(is_fast_start("custom FAST-START", ""));
    check(is_fast_start("Renamed profile", "M140 S60\n; ORQA FAST START: ALREADY LOADED MATERIAL/NOZZLE ONLY."));
    check(!is_fast_start("Stock", "; fast travel speed"));
    check(!fast_start_needs_warning(9.999));
    check(fast_start_needs_warning(10.0));
    check(fast_start_needs_warning(100.0));
    check(fast_start_needs_warning(0));
    check(fast_start_needs_warning(-1));
    check(fast_start_needs_warning(std::numeric_limits<double>::quiet_NaN()));
    check(fast_start_needs_warning(std::numeric_limits<double>::infinity()));
    std::cout << "Passed " << checks << " Fast Start policy checks. No printer contacted.\n";
}
