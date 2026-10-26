#ifndef PRICING_H
#define PRICING_H

// --- PRICES AND PAY ---
// Customers pay for part loads the way Indian road transporters quote them: chargeable tonnes x road km from the
// warehouse x one rate, with a minimum per order. Bulky goods are charged by the space they take (1 m3 counts as
// 250 kg). Drivers are paid for every km they drive, there and back. GST is not included.

constexpr double kRatePerTonneKm = 12.0;       // rupees per chargeable tonne per km
constexpr double kChargeableKgPerM3 = 250.0;
constexpr double kMinimumOrderPrice = 1500.0;  // rupees per order
constexpr double kDriverPayPerKm = 2.5;        // rupees per km driven

// The weight an order is charged for: its real weight, or its space at 250 kg per m3 if that is more
double chargeableKg(double weightKg, double volumeM3);

// What the customer pays, in whole rupees, for an order going distanceKm by road from its warehouse
double orderPrice(double weightKg, double volumeM3, int distanceKm);

// What the driver earns, in whole rupees, for a trip whose last drop-off is distanceKm from the warehouse
double driverPay(int distanceKm);

#endif
