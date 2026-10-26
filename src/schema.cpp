#include "../include/schema.h"
#include <string>

namespace {

const char* const kSchemaV1 = R"SQL(
-- Truck bodies: open deck, tipper, closed container
CREATE TABLE body_types (
    code        TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    description TEXT NOT NULL
);

CREATE TABLE warehouses (
    id         INTEGER PRIMARY KEY,
    code       TEXT NOT NULL UNIQUE,                 -- WH-01, WH-02 ...
    name       TEXT NOT NULL,
    city       TEXT NOT NULL,                        -- a city in network_map.csv; every route starts here
    state      TEXT NOT NULL,
    address    TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- Everyone who can log in; the role says which of the three tables below holds the rest
CREATE TABLE users (
    id            INTEGER PRIMARY KEY,
    username      TEXT NOT NULL UNIQUE COLLATE NOCASE,
    password_hash TEXT NOT NULL,                     -- pbkdf2_sha256$..., never the password itself
    role          TEXT NOT NULL CHECK (role IN ('manager', 'driver', 'customer')),
    full_name     TEXT NOT NULL,
    email         TEXT NOT NULL UNIQUE COLLATE NOCASE,
    phone         TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE managers (
    user_id       INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    warehouse_id  INTEGER NOT NULL REFERENCES warehouses(id),
    employee_code TEXT NOT NULL UNIQUE
);

-- Drivers belong to a warehouse but not to one truck: a free truck and a licensed driver are paired per trip
CREATE TABLE drivers (
    user_id       INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    warehouse_id  INTEGER NOT NULL REFERENCES warehouses(id),
    licence_no    TEXT NOT NULL UNIQUE,
    licence_class TEXT NOT NULL CHECK (licence_class IN ('LMV', 'HMV')),
    approval      TEXT NOT NULL DEFAULT 'pending' CHECK (approval IN ('pending', 'approved', 'rejected')),
    status        TEXT NOT NULL DEFAULT 'available' CHECK (status IN ('available', 'on_trip', 'off_duty'))
);

CREATE TABLE customers (
    user_id      INTEGER PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    company_name TEXT NOT NULL,
    city         TEXT NOT NULL,
    address      TEXT NOT NULL,
    gstin        TEXT UNIQUE                          -- optional GST number
);

CREATE TABLE truck_models (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    body_type       TEXT NOT NULL REFERENCES body_types(code),
    payload_kg      REAL NOT NULL CHECK (payload_kg > 0),
    volume_m3       REAL NOT NULL CHECK (volume_m3 > 0),
    gross_weight_kg REAL NOT NULL CHECK (gross_weight_kg > payload_kg),   -- truck + full load
    cost_per_km     REAL NOT NULL CHECK (cost_per_km > 0),
    -- Indian rule: a light-vehicle licence (LMV) covers trucks up to 7,500 kg gross weight
    licence_needed  TEXT GENERATED ALWAYS AS (CASE WHEN gross_weight_kg <= 7500 THEN 'LMV' ELSE 'HMV' END) VIRTUAL
);

CREATE TABLE trucks (
    id           INTEGER PRIMARY KEY,
    warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),
    model_id     INTEGER NOT NULL REFERENCES truck_models(id),
    plate_no     TEXT NOT NULL UNIQUE,
    status       TEXT NOT NULL DEFAULT 'available' CHECK (status IN ('available', 'on_trip', 'maintenance')),
    added_at     TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE goods_types (
    code        TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    unit        TEXT NOT NULL,                       -- what the customer counts in: bag, bale, tonne, carton
    kg_per_unit REAL NOT NULL CHECK (kg_per_unit > 0),
    m3_per_unit REAL NOT NULL CHECK (m3_per_unit > 0),
    keep_dry    INTEGER NOT NULL CHECK (keep_dry IN (0, 1)),
    is_food     INTEGER NOT NULL CHECK (is_food IN (0, 1)),
    handling    TEXT NOT NULL                        -- shown to the manager and the driver
);

-- Which truck bodies may carry each goods type
CREATE TABLE goods_body_types (
    goods_code TEXT NOT NULL REFERENCES goods_types(code),
    body_type  TEXT NOT NULL REFERENCES body_types(code),
    PRIMARY KEY (goods_code, body_type)
);

-- Goods that may share one truck: each allowed pair is stored both ways, and each type with itself
CREATE TABLE goods_compatibility (
    goods_a TEXT NOT NULL REFERENCES goods_types(code),
    goods_b TEXT NOT NULL REFERENCES goods_types(code),
    PRIMARY KEY (goods_a, goods_b)
);

CREATE INDEX idx_trucks_warehouse   ON trucks(warehouse_id, status);
CREATE INDEX idx_drivers_warehouse  ON drivers(warehouse_id, approval, status);
CREATE INDEX idx_managers_warehouse ON managers(warehouse_id);
)SQL";

const char* const kSchemaV2 = R"SQL(
-- One warehouse per city for now
CREATE UNIQUE INDEX ux_warehouses_city ON warehouses(city);

-- A login: the browser keeps a random token in a cookie, the database keeps only its SHA-256,
-- so someone who reads this file still can't use it to log in as anyone
CREATE TABLE sessions (
    token_hash TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    expires_at TEXT NOT NULL
);

-- One truck journey: out from the warehouse to the delivery, then back home
CREATE TABLE trips (
    id            INTEGER PRIMARY KEY,
    warehouse_id  INTEGER NOT NULL REFERENCES warehouses(id),
    truck_id      INTEGER NOT NULL REFERENCES trucks(id),
    driver_id     INTEGER NOT NULL REFERENCES drivers(user_id),
    status        TEXT NOT NULL DEFAULT 'in_transit' CHECK (status IN ('in_transit', 'returning', 'completed')),
    route_json    TEXT NOT NULL,                     -- cities and roads, as planned when the trip started
    distance_km   INTEGER NOT NULL CHECK (distance_km > 0),   -- warehouse to delivery, one way
    cost          REAL NOT NULL CHECK (cost >= 0),            -- truck running cost per km x the distance both ways
    dispatched_at TEXT NOT NULL DEFAULT (datetime('now')),
    delivered_at  TEXT,
    completed_at  TEXT                               -- back at the warehouse, truck and driver free again
);

CREATE TABLE orders (
    id               INTEGER PRIMARY KEY,
    customer_id      INTEGER NOT NULL REFERENCES customers(user_id),
    warehouse_id     INTEGER NOT NULL REFERENCES warehouses(id),   -- ships the goods; routes start here
    goods_code       TEXT NOT NULL REFERENCES goods_types(code),
    quantity         REAL NOT NULL CHECK (quantity > 0),           -- in the goods type's unit
    weight_kg        REAL NOT NULL CHECK (weight_kg > 0),
    volume_m3        REAL NOT NULL CHECK (volume_m3 > 0),
    destination_city TEXT NOT NULL,
    delivery_address TEXT NOT NULL,
    receiver_name    TEXT NOT NULL,
    receiver_phone   TEXT,
    notes            TEXT,
    status           TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'assigned', 'delivered', 'cancelled')),
    trip_id          INTEGER REFERENCES trips(id),
    created_at       TEXT NOT NULL DEFAULT (datetime('now')),
    delivered_at     TEXT
);

CREATE INDEX idx_sessions_user     ON sessions(user_id);
CREATE INDEX idx_trips_warehouse   ON trips(warehouse_id, status);
CREATE INDEX idx_trips_driver      ON trips(driver_id, status);
CREATE INDEX idx_orders_warehouse  ON orders(warehouse_id, status);
CREATE INDEX idx_orders_customer   ON orders(customer_id);
CREATE INDEX idx_orders_trip       ON orders(trip_id);
)SQL";

// Prices are fixed when an order is placed, so a later change of rate or road never changes what a customer was told.
// Rows from version 2 get them once at startup (priceOldRecords in orders.cpp), which needs the road map.
const char* const kSchemaV3 = R"SQL(
ALTER TABLE orders ADD COLUMN distance_km INTEGER CHECK (distance_km > 0);   -- warehouse to the delivery city by road
ALTER TABLE orders ADD COLUMN price REAL CHECK (price >= 0);                  -- what the customer pays, rupees before GST
ALTER TABLE trips ADD COLUMN driver_pay REAL CHECK (driver_pay >= 0);         -- the driver's pay for the km there and back
)SQL";

int readSchemaVersion(Database& db) {
    Statement version = db.prepare("PRAGMA user_version");
    version.step();
    return int(version.columnInt(0));
}

}  // namespace

void createSchema(Database& db) {
    int version = readSchemaVersion(db);
    if (version == kSchemaVersion) return;
    if (version > kSchemaVersion) {
        throw DbError("The database was made by a newer version of this program (schema " +
                      std::to_string(version) + ", this program knows up to " + std::to_string(kSchemaVersion) + ")");
    }

    // Version 0 is a brand-new file. Each step upgrades by one version, so an old file catches up
    // step by step and keeps its data; all steps run in one transaction, so a failure changes nothing.
    Transaction tx(db);
    if (version < 1) db.exec(kSchemaV1);
    if (version < 2) db.exec(kSchemaV2);
    if (version < 3) db.exec(kSchemaV3);
    db.exec("PRAGMA user_version = " + std::to_string(kSchemaVersion));
    tx.commit();
}
