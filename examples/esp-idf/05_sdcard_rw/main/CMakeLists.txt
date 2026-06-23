# SPDX-FileCopyrightText: 2026 Waveshare Electronics
#
# SPDX-License-Identifier: Apache-2.0

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32_c5_lcd_1_47
)
