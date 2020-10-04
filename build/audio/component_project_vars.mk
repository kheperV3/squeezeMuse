# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/components/audio/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/audio -laudio $(PROJECT_PATH)/components/audio/lib/libesp_processing.a
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += audio
COMPONENT_LDFRAGMENTS += 
component-audio-build: 
