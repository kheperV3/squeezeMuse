# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += $(PROJECT_PATH)/esp-dsp/modules/dotprod/include $(PROJECT_PATH)/esp-dsp/modules/support/include $(PROJECT_PATH)/esp-dsp/modules/windows/include $(PROJECT_PATH)/esp-dsp/modules/windows/hann/include $(PROJECT_PATH)/esp-dsp/modules/windows/blackman/include $(PROJECT_PATH)/esp-dsp/modules/windows/blackman_harris/include $(PROJECT_PATH)/esp-dsp/modules/windows/blackman_nuttall/include $(PROJECT_PATH)/esp-dsp/modules/windows/nuttall/include $(PROJECT_PATH)/esp-dsp/modules/windows/flat_top/include $(PROJECT_PATH)/esp-dsp/modules/iir/include $(PROJECT_PATH)/esp-dsp/modules/fir/include $(PROJECT_PATH)/esp-dsp/modules/math/include $(PROJECT_PATH)/esp-dsp/modules/math/add/include $(PROJECT_PATH)/esp-dsp/modules/math/sub/include $(PROJECT_PATH)/esp-dsp/modules/math/mul/include $(PROJECT_PATH)/esp-dsp/modules/math/addc/include $(PROJECT_PATH)/esp-dsp/modules/math/mulc/include $(PROJECT_PATH)/esp-dsp/modules/matrix/include $(PROJECT_PATH)/esp-dsp/modules/fft/include $(PROJECT_PATH)/esp-dsp/modules/dct/include $(PROJECT_PATH)/esp-dsp/modules/conv/include $(PROJECT_PATH)/esp-dsp/modules/common/include
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/esp-dsp -lesp-dsp
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += esp-dsp
COMPONENT_LDFRAGMENTS += 
component-esp-dsp-build: 
