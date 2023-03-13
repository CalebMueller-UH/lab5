# Uncomment the following line to turn on debug messages
DEBUG = -DDEBUG

# Select the configuration file to use as default for 'run'
DEFAULT_CONFIG = p2p.config

# Define compiler and flags
CC = gcc
CFLAGS = -g -static

# Define source and header directories
SRCDIR = src
INCDIR = include

# Define output directory
OUTDIR = output

# Create output directory if it doesn't exist
ifneq ($(wildcard $(OUTDIR)), $(OUTDIR))
$(shell mkdir -p $(OUTDIR))
endif

# Define output executables
EXECUTABLE = net367
DEBUG_EXECUTABLE = net367debug

# Define source files
SRCS = $(wildcard $(SRCDIR)/*.c)

# Define object files
OBJS = $(patsubst $(SRCDIR)/%.c, $(OUTDIR)/%.o, $(SRCS))

# Define include directories
INCLUDES = -I$(INCDIR)

# Define libraries
LIBS =

# Check if the system is Fedora
ifneq ($(shell lsb_release -i -s),Fedora)
# Default rule to build both executables
all: $(EXECUTABLE) $(DEBUG_EXECUTABLE)

# Rule to build the non-debug executable
$(EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBS) -o $@

# Rule to build the debug executable
$(DEBUG_EXECUTABLE): $(OBJS)
	$(CC) $(CFLAGS) $(DEBUG) $(OBJS) $(LIBS) -o $@

else
# If the system is Fedora, only reset the files
all:
	@echo "Cannot compile on Fedora"
endif

# Rule to build object files
$(OUTDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(DEBUG) $(INCLUDES) -c $< -o $@

# Define starting files for TestDir0
TD0_FILES = testmsg0 testmsg00
# Define starting files for TestDir1
TD1_FILES = bigTest haha.txt testmsg1 testmsg1B 

# Rule to clean object files
clean:
	rm -f $(OUTDIR)/*.o
	rm -f ./$(EXECUTABLE) ./$(DEBUG_EXECUTABLE)
	$(foreach file, $(wildcard TestDir0/*), \
		$(if $(filter $(notdir $(file)), $(TD0_FILES)), , rm -f $(file)))
	$(foreach file, $(wildcard TestDir1/*), \
		$(if $(filter $(notdir $(file)), $(TD1_FILES)), , rm -f $(file)))

# Rule to clean object files
reset:
	$(foreach file, $(wildcard TestDir0/*), \
		$(if $(filter $(notdir $(file)), $(TD0_FILES)), , rm -f $(file)))
	$(foreach file, $(wildcard TestDir1/*), \
		$(if $(filter $(notdir $(file)), $(TD1_FILES)), , rm -f $(file)))
	pkill -f net367

clear:
	clear
	
# Rule to regenerate object files and executables
regen: clear clean all


# Rule to run the non-debug executable with the default configuration
run: $(EXECUTABLE)
	./$(EXECUTABLE) $(DEFAULT_CONFIG)

# Rule to run the debug executable with the default configuration
debug: $(DEBUG_EXECUTABLE)
	./$(DEBUG_EXECUTABLE) $(DEFAULT_CONFIG
