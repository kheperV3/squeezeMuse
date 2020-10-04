# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/components/codecs/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/codecs -lcodecs $(PROJECT_PATH)/components/codecs/lib/libmad.a $(PROJECT_PATH)/components/codecs/lib/libFLAC.a $(PROJECT_PATH)/components/codecs/lib/libhelix-aac.a $(PROJECT_PATH)/components/codecs/lib/libvorbisidec.a $(PROJECT_PATH)/components/codecs/lib/libogg.a $(PROJECT_PATH)/components/codecs/lib/libalac.a $(PROJECT_PATH)/components/codecs/lib/libresample16.a $(PROJECT_PATH)/components/codecs/lib/libopusfile.a $(PROJECT_PATH)/components/codecs/lib/libopus.a 		
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += codecs
COMPONENT_LDFRAGMENTS += 
component-codecs-build: 
