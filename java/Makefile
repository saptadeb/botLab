include ../src/common.mk

LCM_JAR = /usr/local/share/java/lcm.jar

ROB550_JAR = $(JAVA_PATH)/rob550.jar
BUILD_PATH = build
SOURCE_PATH = src

# Construct the pathnames to the C types
LCMTYPES_NAMES := $(basename $(shell cd ../lcmtypes && ls *.lcm))
LCMTYPES_JAVA := $(addsuffix .java, $(addprefix $(SOURCE_PATH)/rob550/lcmtypes/, $(LCMTYPES_NAMES)))
LCMTYPES_CLASS := $(addsuffix .class, $(addprefix $(BUILD_PATH)/rob550/lcmtypes/, $(LCMTYPES_NAMES)))

PLUGINS_NAMES := $(basename $(shell cd $(SOURCE_PATH) && ls rob550/plugins/*.java 2> /dev/null))
PLUGINS_JAVA := $(addsuffix .java, $(addprefix $(SOURCE_PATH)/, $(PLUGINS_NAMES)))
PLUGINS_CLASS := $(addsuffix .class, $(addprefix $(BUILD_PATH)/, $(PLUGINS_NAMES)))

all: $(ROB550_JAR)

laptop-only: $(ROB550_JAR)

src/rob550/lcmtypes/%.java: ../lcmtypes/%.lcm
	@echo "\t$<"
	@lcm-gen -j --jpath=src --jdefaultpkg=rob550.lcmtypes $<

.LCMTYPES_CLASS: $(BUILD_PATH) $(LCMTYPES_JAVA)
	@echo "[java/lcmtypes]"
	@echo "\t$(LCMTYPES_JAVA)"
	@javac -classpath $(LCM_JAR) -d $(BUILD_PATH) $(LCMTYPES_JAVA)
	@touch .LCMTYPES_CLASS

$(PLUGINS_CLASS): $(BUILD_PATH) $(PLUGINS_JAVA) .LCMTYPES_CLASS
	@echo "[java/plugins]"
	@echo "\t$(PLUGINS_CLASS)"
	@javac -classpath $(BUILD_PATH):$(LCM_JAR) -d $(BUILD_PATH) $(PLUGINS_JAVA)

$(ROB550_JAR): $(BUILD_PATH) .LCMTYPES_CLASS $(PLUGINS_CLASS)
	@echo "[java/rob550.jar]"
	@echo "$(ROB550_JAR)"
	@jar cf $(ROB550_JAR) -C $(BUILD_PATH) .

$(BUILD_PATH):
	@mkdir -p $(BUILD_PATH)

clean:
	@echo "clean [java/lcmtypes]"; rm -f $(SOURCE_PATH)/rob550/lcmtypes/*
	@echo "clean [java/build]"; rm -rf build/*
	@echo "clean [java/rob550.jar]"; rm -f $(ROB550_JAR)
	@rm -f *~ .LCMTYPES_CLASS
