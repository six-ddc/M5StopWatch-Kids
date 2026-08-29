/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/wear_levelling/wear_levelling.h"
#include <esp_partition.h>
#include <mooncake_log.h>

static const std::string_view _tag = "HAL-FS";

void Hal::fs_init()
{
    // The FAT partition is optional. The default layout (partitions.csv) hands
    // its space to the app instead, because nothing in this firmware reads or
    // writes /spiflash; partitions-with-storage.csv keeps it. Probe rather than
    // compile in the choice, so swapping the CSV needs no code change.
    //
    // Without the probe, mounting a partition that is not there just logs an
    // error and returns -- harmless, but it reads like a real failure.
    if (esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                                 "storage") == nullptr) {
        mclog::tagInfo(_tag, "no storage partition in this layout, skipping FAT mount");
        return;
    }

    mclog::tagInfo(_tag, "init");
    wear_levelling_init();
}
