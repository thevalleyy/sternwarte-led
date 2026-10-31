#include <Arduino.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <bitset>
#include <functional>
#include <vector>

// Constants
#define VERSION "0.0.1"
int matrX = 7; // Zero counts as first coordinate,
int matrY = 3; // so 4x8 is denoted by x=7 and y=3
constexpr int fixedLengthBinary = 4;
int cols = 20;
int rows = 4;
enum Action { PRESS, SCROLL_R, SCROLL_L }; // Rotary encoder inputs
enum Line { HEADING, ENTRY, MENU, NONE };  // Formatting options
enum Customchars {                         // Custom characters
  UNSELECTED,
  SELECTED,
  TURNEDOFF,
  TURNEDON,
  BACKSLASH
};

String constellations[] = {"Orion",
                           "Gro\342er Hund",
                           "Fuhrmann",
                           "Perseus",
                           "Cassiopeia",
                           "Gro\342er Wagen",
                           "Kleiner Wagen",
                           "Cepheus",
                           "Fische",
                           "Wassermann",
                           "Steinbock",
                           "Sch\365tze",
                           "Skorpion",
                           "Waage",
                           "Jungfrau",
                           "L\357we",
                           "Krebs",
                           "Zwillinge",
                           "Stier",
                           "Widder",
                           "Jagdhunde",
                           "B\341renh\365ter",
                           "N\357rdliche Krone",
                           "Herkules",
                           "Leier",
                           "Schwan",
                           "Adler",
                           "Delphin",
                           "Rabe",
                           "Eidechse",
                           "Pegasus",
                           "Dreieck"};
int order[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
}; // Defines the order in which constellations get turned on/off. This is
   // applied to the alphabetically sorted array, so make sure to enter
   // constellations[] sorted.

// Definitions
JsonDocument table;
LiquidCrystal_I2C lcd(0x27, cols, rows); // address, cols, rows
byte unselected[] = {B00000, B01110, B10001, B10001,
                     B10001, B01110, B00000, B00000};
byte selected[] = {B00000, B01110, B11111, B11111,
                   B11111, B01110, B00000, B00000};
byte turnedoff[] = {B00000, B01110, B10001, B10001,
                    B10001, B01110, B01110, B00100};
byte turnedon[] = {B00000, B01110, B11111, B11111,
                   B11111, B01110, B01110, B00100};
byte backslash[] = {B00000, B10000, B01000, B00100,
                    B00010, B00001, B00000, B00000};

using MenuAction = std::function<void(int, bool)>;
struct MenuEntry {
  String name;
  int state;
  bool isMenu;
  std::vector<MenuEntry> submenu;
  MenuAction action;
};

MenuEntry rootMenu;
std::vector<int> currentPath;

// Pin definitions
#define STATUS_LED 2
#define LCD_SDA 21    // serial data (lcd)
#define LCD_SCL 22    // serial clock (lcd)
#define RE_CLK 25     // s1/clock (rotary encoder)
#define RE_DT 26      // s2/data (rotary encoder)
#define RE_SW 27      // key/switch (rotary encoder)
#define BL_SW 15      // backlight toggle
#define BACK 4        // menu back navigation
#define TOGGLE_ALL 16 // toggle all constellations
#define decX0 0
#define decX1 1
#define decX2 2
#define decX3 3
#define decY0 4
#define decY1 5
#define decY2 6
#define decY3 7

std::vector<int> decXList = {decX0, decX1, decX2, decX3};
std::vector<int> decYList = {decY0, decY1, decY2, decY3};

// Variables
int reCounter;
int reCurrentStateCLK;
int reLastStateCLK;
int menuOffset = 0;
int selectedEntry = 0;
int spinPhase = 0;
bool blState = 1; // Set to 0 to start with backlight off
bool blToggle = 0;
bool reUpdate = 0;
bool reClick = 0;
bool queryGoBack = 0;
bool toggleAll = 1; // Set to 1 to start with all constellations turned on
bool toggleAllState = 0;
bool tripwire = 1; // Don't change!

// Methods
// IO
void blink(int pin, int delayTime, int reps) {
  for (int i = 0; i < reps; i++) {
    digitalWrite(pin, HIGH);
    delay(delayTime);
    digitalWrite(pin, LOW);
    delay(delayTime);
  }
}
void restart(int, bool) { ESP.restart(); }

// Constellation logic
String toBinary(int x) {
  std::bitset<fixedLengthBinary> b(x);
  // Convert the bitset to a string first
  return b.to_string().c_str();
}

void switchLED(int x, int y, bool state) {
  Serial.println("Switching x=" + (String)x + " y=" + y +
                 " new state: " + state);
  String binXString = toBinary(x);
  String binYString = toBinary(y);

  for (int i = 0; i < min(binXString.length(), decXList.size()); i++) {
    digitalWrite(decXList[i], int(binXString[i]));
    Serial.println("Turning X, " + String(decXList[i]) + " to " +
                   binXString[i]);
  }

  for (int i = 0; i < min(binYString.length(), decYList.size()); i++) {
    digitalWrite(decYList[i], int(binYString[i]));
    Serial.println("Turning Y, " + String(decYList[i]) + " to " +
                   binYString[i]);
  }

  if (state) {
    Serial.println("Switch ON -> HIGH");
  } else {
    Serial.println("Switch OFF -> HIGH");
  }

  // delay here to give the hardware some time
  blink(STATUS_LED, 5, 1);

  Serial.println("Switch ON & Switch OFF -> LOW");
}

void toggleConstellation(int index, bool state) {
  Serial.println(constellations[index]);

  JsonDocument currentTable = table[constellations[index]];

  // Actually turn the constellation on/off here
  switchLED(currentTable["x"], currentTable["y"], state);
}

// Formatting
String rightPad(String input, int length, char ch) {
  String padded = input;

  for (int i = 0; i < max(0, (int)(length - input.length())); i++) {
    padded = padded + ch;
  }

  return padded;
}

String formatLine(String input, int mode, int length) {
  if (mode == HEADING) {
    String line = "[" + input + "]";
    return rightPad(line, length, ' ');
  } else if (mode == ENTRY) {
    return rightPad(input, length, ' ');
  } else if (mode == NONE) {
    return rightPad("", length, ' ');
  } else {
    return input;
  }
}

// Setup sequence
void startupFailed(String error) {
  Serial.println(" > Startup failed: " + error);

  String displayError = "Error: " + error;
  int printRows = ceil(displayError.length() / cols);
  int offset = 0;

  // Display error on lcd
  lcd.clear();
  lcd.backlight();

  while (true) {
    for (int i = 0; i < rows; i++) {
      lcd.setCursor(0, i);
      lcd.print(displayError.substring(
          (i + offset) * cols,
          min((i + 1 + offset) * cols, (int)displayError.length())));
    }

    // Blink status led to draw attention, also
    // this works as a delay for the scroll function
    blink(STATUS_LED, 100, 12);

    offset++;
    if (offset > printRows - rows)
      offset = 0;
  }
}

void createJSON(String strings[], int size) {
  int x = 0, y = 0;
  for (int i = 0; i < size; i++) {

    table[strings[i]]["x"] = x;
    table[strings[i]]["y"] = y;
    table[strings[i]]["state"] = 0;

    x++;
    if (x > matrX) {
      x = 0;
      y++;
    }
  }

  // String jsonString;
  // serializeJsonPretty(table, jsonString);
  // Serial.println(jsonString);
}

// Menu
MenuEntry createMenu() {
  MenuEntry menu;
  menu.name = "Sternwarte-LED";
  menu.state = -1;
  menu.isMenu = true;

  MenuEntry menu0;
  menu0.name = "Sternbilder";
  menu0.state = -1;
  menu0.isMenu = true;

  // Sort array
  std::sort(std::begin(constellations), std::end(constellations));

  // Add constellations
  for (String str : constellations) {
    menu0.submenu.push_back({str, 0, false, {}, toggleConstellation});
  }
  menu.submenu.push_back(menu0);

  // MenuEntry menu1;
  // menu1.name = "Einstellungen";
  // menu1.state = -1;
  // menu1.isMenu = true;
  // menu.submenu.push_back(menu1);
  // TODO Einstellungen mit dauerhafter speicherung

  MenuEntry menu2;
  menu2.name = "Debug";
  menu2.state = -1;
  menu2.isMenu = true;
  menu2.submenu.push_back({"v" VERSION, -1, false, {}, nullptr});
  menu2.submenu.push_back(
      {"frHeap " + String(ESP.getFreeHeap() / 1024.0).substring(0, cols - 13) +
           "kiB",
       -1,
       false,
       {},
       nullptr});
  menu2.submenu.push_back(
      {"frFlsh " +
           String(ESP.getFreeSketchSpace() / 1024.0).substring(0, cols - 13) +
           "kiB",
       -1,
       false,
       {},
       nullptr});
  menu2.submenu.push_back({String(ESP.getChipModel()).substring(0, cols - 3),
                           -1,
                           false,
                           {},
                           nullptr});
  menu2.submenu.push_back({"Reboot", -1, false, {}, restart});
  menu.submenu.push_back(menu2);

  return menu;
  // String name;
  // int state;
  // bool isMenu;
  // std::vector<MenuEntry> submenu;
  // MenuAction action;
}

void initMenu() { currentPath.clear(); }

MenuEntry *getCurrentMenu() {
  MenuEntry *current = &rootMenu;
  for (int idx : currentPath) {
    if (idx >= 0 && idx < current->submenu.size()) {
      current = &current->submenu[idx];
    }
  }
  return current;
}

void enterSubmenu(int index) {
  MenuEntry *current = getCurrentMenu();

  // Check if index is valid and it's a submenu
  if (index >= 0 && index < current->submenu.size() &&
      current->submenu[index].isMenu) {
    currentPath.push_back(index);
  }
}

void executeCurrent(int index) {
  MenuEntry *current = getCurrentMenu();
  if (index >= 0 && index < current->submenu.size() &&
      !current->submenu[index].isMenu) {
    if (current->submenu[index].action) {
      // Switch state
      switch (current->submenu[index].state) {
      case 1:
        current->submenu[index].state = 0;
        break;
      case 0:
        current->submenu[index].state = 1;
        break;

      default:
        break;
      }

      // Execute action method
      current->submenu[index].action(index, current->submenu[index].state);
    }
  }
}

void displayMenu(int action) {
  Serial.println("updating gui");
  MenuEntry *current = getCurrentMenu();
  int entries = current->submenu.size();

  if (action == -1) {
    menuOffset = 0;
  }

  if (action == SCROLL_R)
    menuOffset = (menuOffset + 1) % entries;
  if (action == SCROLL_L)
    menuOffset = (menuOffset - 1 + entries) % entries;

  if (action == PRESS) {
    if (current->submenu[selectedEntry].isMenu) {
      enterSubmenu(selectedEntry); // Enter the clicked menu

      current = getCurrentMenu();
      entries = current->submenu.size();
      menuOffset = 0;
    } else {
      executeCurrent(selectedEntry);
    }
  }

  lcd.setCursor(0, 0);
  lcd.print(formatLine(current->name, HEADING, cols));

  if (entries <= rows - 1) { // No scrolling needed
    selectedEntry = menuOffset;

    for (int i = 0; i < entries; i++) {
      lcd.setCursor(3, i + 1);
      lcd.print(formatLine(current->submenu[i].name, ENTRY, cols - 3));
      lcd.setCursor(1, i + 1);
      lcd.write(i == menuOffset ? SELECTED : UNSELECTED);

      lcd.setCursor(cols - 1, i + 1);
      if (current->submenu[i].state == 0)
        lcd.write(TURNEDOFF);
      if (current->submenu[i].state == 1)
        lcd.write(TURNEDON);
    }

    for (int i = entries; i < (rows - 1); i++) { // Clear remaining lines
      lcd.setCursor(0, i + 1);
      lcd.print(formatLine("", NONE, cols));
    }
  } else { // Scrolling menu
    selectedEntry = (1 + menuOffset) % entries;

    for (int i = 0; i < rows - 1; i++) {
      lcd.setCursor(3, i + 1);
      lcd.print(formatLine(current->submenu[(i + menuOffset) % entries].name,
                           ENTRY, cols - 3));
      lcd.setCursor(1, i + 1);
      lcd.write(i == 1 ? SELECTED : UNSELECTED);

      lcd.setCursor(cols - 1, i + 1);
      if (current->submenu[(i + menuOffset) % entries].state == 0)
        lcd.write(TURNEDOFF);
      if (current->submenu[(i + menuOffset) % entries].state == 1)
        lcd.write(TURNEDON);
    }
  }

  // Serial.println("selected: (" + String(selectedEntry) + ") " +
  // String(current->submenu[selectedEntry].name));
}

void displayAction() {
  lcd.setCursor(cols - 1, 0);
  const char spinner[] =
      "\55\55\55\4\4\4\174\174\174\57\57\57"; // backslash = \004
  lcd.write(spinner[spinPhase]);
  spinPhase = (spinPhase + 1) % 12;
}

// Interrupts
void IRAM_ATTR clickEncoder() { reClick = 1; }

// Interrupt service routine
void IRAM_ATTR updateEncoder() { reUpdate = 1; }

// Toggle backlight
void IRAM_ATTR toggleBacklight() {
  detachInterrupt(digitalPinToInterrupt(BL_SW));
  blToggle = 1;
}

// Menu navigate back
void IRAM_ATTR goBack() {
  detachInterrupt(digitalPinToInterrupt(BACK));
  queryGoBack = 1;
}

// Toggle ALL constellations
void IRAM_ATTR toggleAllConstellations() {
  detachInterrupt(digitalPinToInterrupt(TOGGLE_ALL));
  toggleAll = 1;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    // Wait for Serial to be ready
  }

  Serial.println();
  Serial.println("******** Sternwarte LED v" VERSION " ********");

  // Assign pin modes
  Serial.println(" > Assigning pin modes");
  pinMode(STATUS_LED, OUTPUT);
  pinMode(RE_CLK, INPUT);
  pinMode(RE_DT, INPUT);
  pinMode(RE_SW, INPUT_PULLUP);
  pinMode(BACK, INPUT_PULLDOWN);
  pinMode(BL_SW, INPUT_PULLDOWN);
  pinMode(TOGGLE_ALL, INPUT_PULLDOWN);

  // Init LCD
  Serial.println(" > Initializing LCD");
  lcd.init();
  lcd.clear();
  if (blState)
    lcd.backlight();
  lcd.setCursor(0, 1);
  lcd.print("   Sternwarte-LED");
  lcd.setCursor(0, 2);
  lcd.print("       v" VERSION);
  lcd.createChar(UNSELECTED, unselected);
  lcd.createChar(SELECTED, selected);
  lcd.createChar(TURNEDOFF, turnedoff);
  lcd.createChar(TURNEDON, turnedon);
  lcd.createChar(BACKSLASH, backslash);

  // delay(500); // Make sure the version string is visible at startup
  // In this case, turning every constellation on at startup is taking
  // long enough.

  // Init RE
  Serial.println(" > Attaching rotary encoder interrupt");
  reLastStateCLK = digitalRead(RE_CLK);
  attachInterrupt(digitalPinToInterrupt(RE_CLK), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RE_SW), clickEncoder, FALLING);

  // Init interrupts
  Serial.println(" > Attaching button interrupts");
  attachInterrupt(digitalPinToInterrupt(BL_SW), toggleBacklight, FALLING);
  attachInterrupt(digitalPinToInterrupt(BACK), goBack, FALLING);
  attachInterrupt(digitalPinToInterrupt(TOGGLE_ALL), toggleAllConstellations,
                  FALLING);

  // Generate JSON object
  Serial.println(" > Generating JSON object of constellations");
  int numberOfConstellations =
      sizeof(constellations) / sizeof(constellations[0]);

  if ((matrX + 1) * (matrY + 1) < numberOfConstellations)
    startupFailed("Number of constellations (" +
                  String(numberOfConstellations) + ") cannot be achieved by " +
                  String(matrX + 1) + "x" + String(matrY + 1) + " matrix");

  createJSON(constellations, numberOfConstellations);

  if (numberOfConstellations != (sizeof(order) / sizeof(order[0])))
    startupFailed("Number of constellations (" +
                  String(numberOfConstellations) +
                  ") has to match length of order (" +
                  String((sizeof(order) / sizeof(order[0]))) + ")");

  // Enter constellation menu
  Serial.println(" > Entering constellation menu");
  rootMenu = createMenu();
  enterSubmenu(0);

  Serial.println(" > Leaving setup scope");
}

void loop() {
  // handle blToggle
  if (blToggle) {
    if (blState) {
      lcd.noBacklight();
    } else {
      lcd.backlight();
    }

    blState = !blState;
    blToggle = 0;

    delay(50);
    attachInterrupt(digitalPinToInterrupt(BL_SW), toggleBacklight, FALLING);
  }

  if (reUpdate) {
    reCurrentStateCLK = digitalRead(RE_CLK);

    // If CLK changed, check DT
    if (reCurrentStateCLK != reLastStateCLK) {
      if (digitalRead(RE_DT) != reCurrentStateCLK) {
        reCounter--;
        if (reCounter <= -2) {
          displayMenu(SCROLL_L);
        }
      } else {
        reCounter++;
        if (reCounter >= 2) {
          displayMenu(SCROLL_R);
        }
      }
    }

    reLastStateCLK = reCurrentStateCLK;
    reCounter = reCounter % 2;
    reUpdate = 0;
  }

  if (reClick) {
    displayMenu(PRESS);
    reClick = 0;
  }

  if (queryGoBack) {
    if (currentPath.size() > 0) {
      currentPath.pop_back();
      displayMenu(-1);
    }

    queryGoBack = 0;

    delay(50);
    attachInterrupt(digitalPinToInterrupt(BACK), goBack, FALLING);
  }

  if (toggleAll) {
    MenuEntry constellationMenu = rootMenu.submenu[0]; // This is hardcoded!
    toggleAllState = !toggleAllState; // Flip the destination state

    for (int i = 0; i < constellationMenu.submenu.size(); i++) {
      int index = order[i]; // Use index instead of i to enable custom order of
                            // constellations
      if (constellationMenu.submenu[index].state != toggleAllState) {
        constellationMenu.submenu[index].state = toggleAllState; // Switch state
        constellationMenu.submenu[index].action(
            index, toggleAllState); // Perform the action

        displayAction(); // allow user to see progress
        delay(50);
      } else {
        Serial.println("Skipping " + constellationMenu.submenu[index].name);
      }
    }

    rootMenu.submenu[0] = constellationMenu; // update

    toggleAll = 0;
    if (!tripwire)
      displayMenu(-2); // don't run at first iteration of loop()

    delay(50);
    attachInterrupt(digitalPinToInterrupt(TOGGLE_ALL), toggleAllConstellations,
                    FALLING);
  }

  if (tripwire) { // run once at the end of loop()
    tripwire = 0;
    // Display the menu
    Serial.println(" > Displaying constellation menu");
    lcd.clear();
    displayMenu(-2);
  }
}

// TODO Sleep mode nach einiger zeit, aufgeweckt durch gpio pins -> lcd auch
// ausschalten
// TODO geburtstagseingabe mit sternzeichen