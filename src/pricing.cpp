#include "../include/pricing.h"
#include <algorithm>
#include <cmath>

double chargeableKg(double weightKg, double volumeM3) {
    return std::max(weightKg, volumeM3 * kChargeableKgPerM3);
}

double orderPrice(double weightKg, double volumeM3, int distanceKm) {
    double price = chargeableKg(weightKg, volumeM3) / 1000.0 * distanceKm * kRatePerTonneKm;
    return std::round(std::max(price, kMinimumOrderPrice));
}

double driverPay(int distanceKm) {
    return std::round(2.0 * distanceKm * kDriverPayPerKm);  // there and back
}
