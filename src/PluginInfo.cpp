#include "pch.h"

#include "SKSE/SKSE.h"

SKSEPluginInfo(
        .Version = REL::Version{ 0, 1, 0, 0 },
    .Name = "DynamicSmoking"sv,
    .Author = "flydut"sv,
    .SupportEmail = ""sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
    .MinimumSKSEVersion = REL::Version{ 0, 0, 0, 0 })
