# SPDX-FileCopyrightText: 2026 Waveshare Electronics
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif nvs_flash
)
