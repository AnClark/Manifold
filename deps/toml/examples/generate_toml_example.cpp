#include <toml.hpp>
#include <fstream>
#include <iostream>

int main()
{
    // 1. Create root table
    toml::table root;

    // 2. Add basic key-value pairs
    root.insert("title", "TOML++ Generator Example");
    root.insert_or_assign("version", 3);           // integer
    root.insert_or_assign("pi", 3.14159);          // floating point
    root.insert_or_assign("enabled", true);        // boolean

    // 3. Add date-time types
    toml::date d{2026, 5, 13};
    toml::time t{18, 30, 10};
    toml::date_time dt{d, t, toml::time_offset{8, 0}}; // 2026-05-13T18:30:10+08:00
    root.insert_or_assign("created_at", dt);

    // 4. Add regular arrays
    toml::array numbers{1, 2, 3, 4, 5};
    root.insert_or_assign("numbers", numbers);

    toml::array fruits{"apple", "banana", "cherry"};
    root.insert_or_assign("fruits", fruits);

    // 5. Add nested table (sub-table)
    toml::table owner;
    owner.insert_or_assign("name", "Thomas");
    owner.insert_or_assign("email", "thomas@example.com");
    root.insert_or_assign("owner", owner);

    // 6. Add deeper nested table
    toml::table database;
    database.insert_or_assign("server", "192.168.1.1");
    database.insert_or_assign("ports", toml::array{8000, 8001, 8002});
    database.insert_or_assign("connection_max", 5000);
    database.insert_or_assign("enabled", true);
    root.insert_or_assign("database", database);

    // 7. Add inline table
    toml::table point;
    point.is_inline(true); // Mark as inline table
    point.insert_or_assign("x", 10);
    point.insert_or_assign("y", 20);
    point.insert_or_assign("z", 30);
    root.insert_or_assign("point", point);

    // 8. Add Array of Tables
    toml::array servers;
    {
        toml::table srv1;
        srv1.insert_or_assign("name", "alpha");
        srv1.insert_or_assign("ip", "10.0.0.1");
        srv1.insert_or_assign("role", "frontend");
        servers.push_back(srv1);

        toml::table srv2;
        srv2.insert_or_assign("name", "beta");
        srv2.insert_or_assign("ip", "10.0.0.2");
        srv2.insert_or_assign("role", "backend");
        servers.push_back(srv2);
    }
    root.insert_or_assign("servers", servers);

    // 9. Use initializer list for quick construction (demonstration only)
    toml::table metadata{
        {"author", "Kimi"},
        {"license", "MIT"},
        {"stars", 42}
    };
    root.insert_or_assign("metadata", metadata);

    // 10. Output to console
    std::cout << "=== Generated TOML Content ===" << std::endl;
    std::cout << root << std::endl;

    // 11. Output to file
    std::ofstream ofs("output.toml");
    if (ofs.is_open())
    {
        ofs << root;
        std::cout << "File saved to output.toml" << std::endl;
    }
    else
    {
        std::cerr << "Unable to open file for writing!" << std::endl;
        return 1;
    }

    return 0;
}
