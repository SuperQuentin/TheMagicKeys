# Project Name
TARGET = play_notes_from_arduino

# Sources
CPP_SOURCES = main.cpp common.cpp play_midi_files.cpp

# Library Locations
LIBDAISY_DIR = ../../../daisy_seed/libDaisy/
DAISYSP_DIR = ../../../daisy_seed/DaisySP/

# Includes FatFS source files within project.
USE_FATFS = 1

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
