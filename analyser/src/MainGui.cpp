// Meter F8K Analyser: the window app.
//  "Add tracks" - drop audio files or folders and each track is captured into the library.
//  "Library"    - browse everything in the library: sort by any column, search, fix tags, correct BPM/key,
//                 add your own columns (streams, sales, notes ...), import values from a CSV, remove tracks.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine.h"

namespace {

const juce::Colour kBg(24, 27, 33), kPanel(34, 38, 46), kRowAlt(29, 33, 40), kText(220, 224, 232), kDim(140, 146, 158),
                   kAccent(255, 225, 40), kSelect(60, 70, 92), kYours(120, 200, 255);

juce::File defaultLibrary() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Meter F8K Library"); }

void styleButton(juce::Button& b)
{
    b.setColour(juce::TextButton::buttonColourId, kPanel);
    b.setColour(juce::TextButton::textColourOffId, kText);
}

//=============================================================================================== worker thread
class Worker : public juce::Thread {
public:
    std::function<void(const juce::String&)> log;                      //called on the message thread
    std::function<void(int, int, double, const juce::String&)> status; //done, total, current progress, current file
    explicit Worker(juce::Component& ownerComp) : juce::Thread("analyser"), owner(&ownerComp) {}
    ~Worker() override { stopThread(6000); }
    void setLibrary(std::shared_ptr<f8k::Library> l) { const juce::ScopedLock sl(lock); library = std::move(l); needUpgrade = true; notify(); }
    void add(const juce::Array<juce::File>& files) {
        { const juce::ScopedLock sl(lock); for (auto& f : files) queue.add(f); total += files.size(); }
        notify();
    }
    void run() override {
        while (!threadShouldExit()) {
            juce::File next; std::shared_ptr<f8k::Library> lib; bool upgrade = false;
            {
                const juce::ScopedLock sl(lock);
                lib = library; upgrade = needUpgrade; needUpgrade = false;
                if (!upgrade && queue.size() > 0) next = queue.removeAndReturn(0);
            }
            if (lib == nullptr) {wait(-1); continue;}
            if (upgrade) { //tracks scanned by an earlier version get the newer values worked out from their saved data
                const int n = lib->upgradeOldRows([this] { return threadShouldExit(); });
                if (n > 0) postLog("Worked out newer values (sortable grades, BPM) for " + juce::String(n) + " tracks already in the library");
                continue;
            }
            if (next == juce::File()) {post(done, total, 0.0, {}); wait(-1); continue;}
            const juce::String name = next.getFileName();
            post(done, total, 0.0, name);
            const auto r = lib->analyse(next, [this, name](double p) { post(done, total, p, name); }, [this] { return threadShouldExit(); });
            ++done;
            postLog((r.status == f8k::AnalyseOutcome::Added ? "added      " : r.status == f8k::AnalyseOutcome::Updated ? "updated    "
                     : r.status == f8k::AnalyseOutcome::AlreadyThere ? "skipped    " : "NOT ADDED  ") + name + "   (" + r.message + ")");
        }
    }
private:
    void post(int d, int t, double p, const juce::String& n) {
        juce::MessageManager::callAsync([this, sp = owner, d, t, p, n] { if (sp != nullptr && status) status(d, t, p, n); });
    }
    void postLog(const juce::String& line) {
        juce::MessageManager::callAsync([this, sp = owner, line] { if (sp != nullptr && log) log(line); });
    }
    juce::CriticalSection lock;
    juce::Array<juce::File> queue;
    std::shared_ptr<f8k::Library> library;
    bool needUpgrade = false;
    std::atomic<int> done{0}, total{0};
    juce::Component::SafePointer<juce::Component> owner; //the worker lives inside its owner, so a live owner means a live worker
};

//=============================================================================================== "Add tracks"
class AddPanel : public juce::Component, public juce::FileDragAndDropTarget {
public:
    std::function<void(const juce::Array<juce::File>&)> onFiles;
    std::function<void()> onOpenFolder, onChangeFolder;
    AddPanel() {
        for (auto* b : {&addButton, &openButton, &changeButton}) {addAndMakeVisible(b); styleButton(*b);}
        addButton.onClick = [this] { chooseFiles(); };
        openButton.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
        changeButton.onClick = [this] { if (onChangeFolder) onChangeFolder(); };
        addAndMakeVisible(progressBar);
        progressBar.setColour(juce::ProgressBar::foregroundColourId, kAccent.withAlpha(0.8f));
        progressBar.setColour(juce::ProgressBar::backgroundColourId, kPanel);
        progressBar.setPercentageDisplay(false);
        addAndMakeVisible(logBox);
        logBox.setMultiLine(true); logBox.setReadOnly(true); logBox.setCaretVisible(false); logBox.setScrollbarsShown(true);
        logBox.setColour(juce::TextEditor::backgroundColourId, kPanel);
        logBox.setColour(juce::TextEditor::textColourId, kText);
        logBox.setColour(juce::TextEditor::outlineColourId, kPanel);
        logBox.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    }
    void log(const juce::String& l) { logBox.moveCaretToEnd(); logBox.insertTextAtCaret(l + "\n"); }
    void setStatus(int d, int t, double p, const juce::String& n) {
        done = d; total = t; progress = t > 0 ? juce::jlimit(0.0, 1.0, (d + p) / (double)t) : 0.0; current = n; repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        auto drop = dropArea();
        g.setColour(dragOver ? kAccent.withAlpha(0.15f) : kPanel);
        g.fillRoundedRectangle(drop.toFloat(), 10.0f);
        g.setColour(dragOver ? kAccent : kDim);
        const float dash[2] = {8.0f, 6.0f};
        juce::Path p; p.addRoundedRectangle(drop.toFloat().reduced(1.0f), 10.0f);
        juce::Path d; juce::PathStrokeType(1.5f).createDashedStroke(d, p, dash, 2); g.fillPath(d);
        g.setColour(kText);
        g.setFont(juce::Font(22.0f));
        const int cy = drop.getCentreY();
        g.drawText("Drop audio files or folders here", drop.getX(), cy - 34, drop.getWidth(), 30, juce::Justification::centred);
        g.setColour(kDim);
        g.setFont(juce::Font(14.0f));
        g.drawText("WAV, AIFF, FLAC (lossless is best). Each track is captured once and added to your library.",
                   drop.getX(), cy + 6, drop.getWidth(), 22, juce::Justification::centred);
        g.setColour(kText);
        juce::String st = total == 0 ? juce::String("Nothing added yet")
                        : (done < total ? "Working: " + juce::String(done + 1) + " of " + juce::String(total) + "   " + current
                                        : "Done: " + juce::String(total) + " file" + (total == 1 ? "" : "s"));
        g.drawText(st, statusArea(), juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16);
        r.removeFromTop(dropArea().getHeight() + 12);
        auto buttons = r.removeFromTop(30);
        addButton.setBounds(buttons.removeFromLeft(140)); buttons.removeFromLeft(8);
        openButton.setBounds(buttons.removeFromLeft(160)); buttons.removeFromLeft(8);
        changeButton.setBounds(buttons.removeFromLeft(170));
        r.removeFromTop(10);
        r.removeFromTop(22);
        progressBar.setBounds(r.removeFromTop(14));
        r.removeFromTop(10);
        logBox.setBounds(r);
    }
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override {
        dragOver = false; repaint();
        juce::Array<juce::File> list;
        for (auto& f : files) f8k::Library::collectAudioFiles(juce::File(f), list);
        queue(list, files.size());
    }
private:
    juce::Rectangle<int> dropArea() const { return getLocalBounds().reduced(16).removeFromTop(170); }
    juce::Rectangle<int> statusArea() const { auto r = getLocalBounds().reduced(16); r.removeFromTop(170 + 12 + 30 + 10); return r.removeFromTop(22); }
    void queue(const juce::Array<juce::File>& list, int dropped) {
        if (list.isEmpty()) {log("No audio files found in what was added (" + juce::String(dropped) + " item(s))"); return;}
        log("Queued " + juce::String(list.size()) + " file(s)");
        if (onFiles) onFiles(list);
    }
    void chooseFiles() {
        chooser = std::make_unique<juce::FileChooser>("Add audio files", juce::File(), "*.wav;*.wave;*.flac;*.aif;*.aiff;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                juce::Array<juce::File> list; for (auto& f : fc.getResults()) f8k::Library::collectAudioFiles(f, list);
                if (fc.getResults().size()) queue(list, fc.getResults().size());
            });
    }
    juce::TextButton addButton{"Add files..."}, openButton{"Open library folder"}, changeButton{"Change library folder..."};
    double progress = 0.0;
    juce::ProgressBar progressBar{progress};
    juce::TextEditor logBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int done = 0, total = 0;
    juce::String current;
    bool dragOver = false;
};

//=============================================================================================== "Library"
enum class Kind { Text, Number, Duration, Letter, SubGrade, Bonus, Grade, Bpm, Key };

struct Column {
    juce::String key;      //the user.csv key for editable columns, else a name for the column
    juce::String base;     //the index.csv column it shows
    juce::String header;
    Kind kind = Kind::Text;
    bool editable = false, custom = false;
    int width = 80;
    juce::String sortBase; //index.csv column to sort by (if different from base)
};

class LibraryView : public juce::Component, public juce::TableListBoxModel, private juce::Timer {
public:
    std::function<std::shared_ptr<f8k::Library>()> getLibrary;

    LibraryView() {
        addAndMakeVisible(table);
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId, kBg);
        table.setColour(juce::ListBox::outlineColourId, kPanel);
        table.setOutlineThickness(1);
        table.setRowHeight(22);
        table.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, kPanel);
        table.getHeader().setColour(juce::TableHeaderComponent::textColourId, kText);
        table.getHeader().setColour(juce::TableHeaderComponent::outlineColourId, kBg);
        table.getHeader().setColour(juce::TableHeaderComponent::highlightColourId, kSelect);
        table.getHeader().setStretchToFitActive(false);
        table.getViewport()->setScrollBarsShown(true, true);
        addAndMakeVisible(search);
        search.setTextToShowWhenEmpty("Search name, artist, genre, key ...", kDim);
        search.setColour(juce::TextEditor::backgroundColourId, kPanel);
        search.setColour(juce::TextEditor::textColourId, kText);
        search.setColour(juce::TextEditor::outlineColourId, kPanel);
        search.onTextChange = [this] { applyFilter(); };
        for (auto* b : {&addColumnButton, &columnsButton, &importButton, &showButton, &removeButton}) {addAndMakeVisible(b); styleButton(*b);}
        addColumnButton.onClick = [this] { addColumnDialog(); };
        columnsButton.onClick = [this] { columnsMenu(); };
        importButton.onClick = [this] { importDialog(); };
        showButton.onClick = [this] { if (auto* r = selectedRow()) revealTrack(*r); };
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible(countLabel);
        countLabel.setColour(juce::Label::textColourId, kDim);
        countLabel.setJustificationType(juce::Justification::centredRight);
        table.getHeader().addListener(&headerWatcher);
        startTimer(700);
    }

    ~LibraryView() override { table.getHeader().removeListener(&headerWatcher); }

    void reload() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        lastChange = lib->changeCount();
        const juce::String selectedId = selectedRow() ? selectedRow()->id : juce::String();
        idx = lib->index();
        user = lib->userValues();
        custom = lib->customColumns();
        rebuildColumns();
        all.clear();
        for (size_t r = 0; r < idx.rows.size(); ++r) all.push_back({idx.get(r, "id"), r});
        applyFilter();
        if (selectedId.isNotEmpty())
            for (int i = 0; i < (int)shown.size(); ++i) if (shown[(size_t)i].id == selectedId) table.selectRow(i);
    }

    //---- TableListBoxModel
    int getNumRows() override { return (int)shown.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) override {
        g.fillAll(selected ? kSelect : (row % 2 ? kRowAlt : kBg));
    }
    void paintCell(juce::Graphics& g, int row, int colId, int width, int height, bool) override {
        if (row < 0 || row >= (int)shown.size() || colId < 1 || colId > (int)cols.size()) return;
        const Column& c = cols[(size_t)colId - 1];
        bool yours = false, unsure = false;
        const juce::String text = display(shown[(size_t)row], c, yours, unsure);
        g.setColour(yours ? kYours : (unsure ? kDim : kText));
        g.setFont(juce::Font(14.0f));
        const bool right = c.kind == Kind::Number || c.kind == Kind::Duration || c.kind == Kind::Bpm || c.kind == Kind::SubGrade
                        || c.kind == Kind::Letter || c.kind == Kind::Bonus;
        g.drawText(text, 6, 0, width - 12, height, right ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
    }
    juce::Component* refreshComponentForCell(int row, int colId, bool, juce::Component* existing) override {
        if (colId < 1 || colId > (int)cols.size() || !cols[(size_t)colId - 1].editable) {delete existing; return nullptr;}
        auto* cell = dynamic_cast<EditCell*>(existing);
        if (cell == nullptr) {delete existing; cell = new EditCell(*this);}
        cell->set(row, colId);
        return cell;
    }
    void sortOrderChanged(int colId, bool forwards) override { sortCol = colId; sortForwards = forwards; sortShown(); table.updateContent(); repaint(); saveLayout(); }
    void columnsChangedOrResized() { saveLayout(); }
    void saveLayout() {
        if (restoringLayout || colKeys.isEmpty()) return;
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        auto* o = new juce::DynamicObject(); o->setProperty("keys", colKeys.joinIntoString("|")); o->setProperty("layout", table.getHeader().toString());
        lib->folder.getChildFile("view.json").replaceWithText(juce::JSON::toString(juce::var(o)));
    }
    void cellDoubleClicked(int row, int colId, const juce::MouseEvent&) override {
        if (colId >= 1 && colId <= (int)cols.size() && cols[(size_t)colId - 1].editable) return;
        if (row >= 0 && row < (int)shown.size()) showDetails(shown[(size_t)row]);
    }
    void selectedRowsChanged(int) override { const bool any = selectedRow() != nullptr; showButton.setEnabled(any); removeButton.setEnabled(any); }

    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        g.setColour(kDim);
        g.setFont(juce::Font(12.5f));
        g.drawText("Click a column title to sort, click again to reverse. Double-click artist, title, album, year, genre, BPM, key or one of your columns to edit it"
                   " (clear it to go back to the original). Double-click anything else for the track's details.   Blue = your value.   Grey with ? = a less certain estimate.",
                   hintArea(), juce::Justification::centredLeft, true);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto top = r.removeFromTop(28);
        search.setBounds(top.removeFromLeft(280)); top.removeFromLeft(8);
        addColumnButton.setBounds(top.removeFromLeft(110)); top.removeFromLeft(6);
        columnsButton.setBounds(top.removeFromLeft(120)); top.removeFromLeft(6);
        importButton.setBounds(top.removeFromLeft(110)); top.removeFromLeft(6);
        showButton.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
        removeButton.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
        countLabel.setBounds(top);
        r.removeFromTop(6);
        hint = r.removeFromBottom(34);
        table.setBounds(r);
    }

private:
    struct RowRef { juce::String id; size_t row; };

    //---- an editable cell: shows the value, double-click to type a new one (empty = back to the measured/tag value)
    struct EditCell : public juce::Label {
        LibraryView& owner; int row = -1, colId = 0;
        explicit EditCell(LibraryView& o) : owner(o) {
            setEditable(false, true, false);
            setColour(juce::Label::textWhenEditingColourId, kText);
            setColour(juce::Label::backgroundWhenEditingColourId, kPanel);
            setColour(juce::TextEditor::highlightColourId, kSelect);
            setFont(juce::Font(14.0f));
            onTextChange = [this] { owner.edited(row, colId, getText()); };
        }
        void set(int r, int c) {
            row = r; colId = c;
            bool yours = false, unsure = false;
            const Column& col = owner.cols[(size_t)c - 1];
            setText(owner.display(owner.shown[(size_t)r], col, yours, unsure), juce::dontSendNotification);
            setColour(juce::Label::textColourId, yours ? kYours : (unsure ? kDim : kText));
            setJustificationType((col.kind == Kind::Number || col.kind == Kind::Bpm) ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
        void mouseDown(const juce::MouseEvent& e) override { owner.table.selectRowsBasedOnModifierKeys(row, e.mods, false); juce::Label::mouseDown(e); }
        void editorShown(juce::TextEditor* ed) override { //start from your value, or empty for "type to override"
            const Column& col = owner.cols[(size_t)colId - 1];
            ed->setText(owner.userValue(owner.shown[(size_t)row].id, col.key), false);
            ed->selectAll();
        }
    };

    juce::Rectangle<int> hintArea() const { return hint; }

    void timerCallback() override {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib != nullptr && (lib.get() != lastLib || lib->changeCount() != lastChange)) {lastLib = lib.get(); reload();}
    }

    void rebuildColumns() {
        std::vector<Column> c;
        auto add = [&](juce::String key, juce::String base, juce::String header, Kind k, bool ed, int w, juce::String sortBase = {}) {
            Column col; col.key = key; col.base = base; col.header = header; col.kind = k; col.editable = ed; col.width = w; col.sortBase = sortBase;
            c.push_back(col);
        };
        add("file", "file", "Track", Kind::Text, false, 230);
        add("artist", "artist", "Artist", Kind::Text, true, 130);
        add("title", "title", "Title", Kind::Text, true, 150);
        add("grade", "original_grade", "Grade", Kind::Grade, false, 80, "letter_value");
        add("letter", "letter", "Letter", Kind::Letter, false, 52, "letter_value");
        add("bonus", "bonus", "Bonus", Kind::Bonus, false, 52, "bonus_value");
        add("authority", "authority", "Authority", Kind::SubGrade, false, 70, "authority_value");
        add("power", "power", "Power", Kind::SubGrade, false, 56, "power_value");
        add("detail", "detail", "Detail", Kind::SubGrade, false, 56, "detail_value");
        add("bpm", "bpm_est", "BPM", Kind::Bpm, true, 56);
        add("key", "key_est", "Key", Kind::Key, true, 84);
        add("lufs", "lufs_integrated", "LUFS", Kind::Number, false, 56);
        add("peak", "peak_dbfs", "Peak dB", Kind::Number, false, 62);
        add("tp", "true_peak_dbtp", "True pk", Kind::Number, false, 62);
        add("len", "duration_s", "Length", Kind::Duration, false, 60);
        for (auto& cc : custom) {
            Column col; col.key = cc.key; col.header = cc.name; col.kind = cc.numeric ? Kind::Number : Kind::Text; col.editable = true; col.custom = true; col.width = 100;
            c.push_back(col);
        }
        add("album", "album", "Album", Kind::Text, true, 120);
        add("year", "year", "Year", Kind::Number, true, 50);
        add("genre", "genre", "Genre", Kind::Text, true, 90);
        add("fmt", "format", "Format", Kind::Text, false, 110);
        add("dir", "directions", "Directions", Kind::Text, false, 110);
        add("added", "analysed", "Added", Kind::Text, false, 130);
        //keep the user's column widths and sort if the set of columns did not change
        juce::StringArray keys; for (auto& x : c) keys.add(x.key);
        if (keys == colKeys) {cols = c; return;}
        const juce::String sortKey = (sortCol >= 1 && sortCol <= (int)cols.size()) ? cols[(size_t)sortCol - 1].key : juce::String("grade");
        cols = c; colKeys = keys;
        auto& h = table.getHeader();
        h.removeAllColumns();
        for (int i = 0; i < (int)cols.size(); ++i) h.addColumn(cols[(size_t)i].header, i + 1, cols[(size_t)i].width, 30, 800, juce::TableHeaderComponent::defaultFlags);
        restoringLayout = true;
        bool restored = false;
        if (auto lib = getLibrary ? getLibrary() : nullptr) { //the widths, order and sort you left the columns in, if the columns are the same
            const juce::var v = juce::JSON::parse(lib->folder.getChildFile("view.json"));
            if (v.getProperty("keys", "").toString() == keys.joinIntoString("|")) {h.restoreFromString(v.getProperty("layout", "").toString()); restored = h.getSortColumnId() > 0;}
        }
        if (restored) {sortCol = h.getSortColumnId(); sortForwards = h.isSortedForwards();}
        else {
            sortCol = keys.indexOf(sortKey) + 1;
            if (sortCol < 1) {sortCol = keys.indexOf("grade") + 1; sortForwards = true;}
            h.setSortColumnId(sortCol, sortForwards);
        }
        restoringLayout = false;
    }

    juce::String userValue(const juce::String& id, const juce::String& key) const {
        auto it = user.find(id);
        if (it == user.end()) return {};
        auto jt = it->second.find(key);
        return jt == it->second.end() ? juce::String() : jt->second;
    }

    juce::String display(const RowRef& r, const Column& c, bool& yours, bool& unsure) const {
        yours = false; unsure = false;
        if (c.editable) {
            const juce::String u = userValue(r.id, c.key);
            if (u.isNotEmpty()) {yours = true; return u;}
            if (c.custom) return {};
        }
        const juce::String v = idx.get(r.row, c.base);
        switch (c.kind) {
            case Kind::Duration: {const int s = (int)std::lround(v.getDoubleValue()); return v.isEmpty() ? v : juce::String(s/60) + ":" + juce::String(s%60).paddedLeft('0', 2);}
            case Kind::Number: return (c.base == "lufs_integrated" || c.base == "peak_dbfs" || c.base == "true_peak_dbtp") && v.isNotEmpty() ? juce::String(v.getDoubleValue(), 1) : v;
            case Kind::Bpm: {
                if (v.isEmpty()) return {};
                unsure = idx.get(r.row, "bpm_confidence").getDoubleValue() < 0.3;
                return juce::String(juce::roundToInt(v.getDoubleValue())) + (unsure ? "?" : "");
            }
            case Kind::Key: unsure = v.isNotEmpty() && idx.get(r.row, "key_confidence").getDoubleValue() < 0.05; return v.isEmpty() ? v : v + (unsure ? "?" : "");
            default: return v;
        }
    }

    //sort value: numbers sort as numbers, grades by the value behind them, blanks always last
    double sortNumber(const RowRef& r, const Column& c, bool& blank) const {
        juce::String v;
        if (c.editable && (v = userValue(r.id, c.key)).isNotEmpty()) {}
        else if (c.sortBase.isNotEmpty()) v = idx.get(r.row, c.sortBase);
        else v = idx.get(r.row, c.base);
        if (c.kind == Kind::Bonus) { //only 1-80 is shown, lower is better
            if (idx.get(r.row, "bonus").isEmpty()) {blank = true; return 0;}
        }
        v = v.trim().removeCharacters(",");
        blank = v.isEmpty() || !v.containsAnyOf("0123456789");
        return v.getDoubleValue();
    }

    void sortShown() {
        if (sortCol < 1 || sortCol > (int)cols.size()) return;
        const Column c = cols[(size_t)sortCol - 1];
        const bool numeric = c.kind != Kind::Text && c.kind != Kind::Key;
        //grades read best-first when sorted "forwards" (A before B, high authority before low, bonus 1 before 80)
        const bool higherFirst = c.kind == Kind::Grade || c.kind == Kind::Letter || c.kind == Kind::SubGrade;
        std::stable_sort(shown.begin(), shown.end(), [&](const RowRef& a, const RowRef& b) {
            if (numeric) {
                bool ba = false, bb = false;
                const double va = sortNumber(a, c, ba), vb = sortNumber(b, c, bb);
                if (ba != bb) return bb; //blanks last
                if (ba) return false;
                if (va == vb) return false;
                const bool less = higherFirst ? va > vb : va < vb;
                return sortForwards ? less : !less;
            }
            bool y, u;
            const juce::String sa = display(a, c, y, u), sb = display(b, c, y, u);
            if (sa.isEmpty() != sb.isEmpty()) return sb.isEmpty();
            const int cmp = sa.compareNatural(sb);
            return sortForwards ? cmp < 0 : cmp > 0;
        });
    }

    void applyFilter() {
        const juce::String q = search.getText().trim();
        shown.clear();
        juce::StringArray words; words.addTokens(q, " ", "\"");
        for (auto& r : all) {
            bool ok = true;
            if (words.size()) {
                juce::String hay;
                bool y, u;
                for (auto& c : cols) hay << display(r, c, y, u) << " ";
                for (auto& w : words) if (w.isNotEmpty() && !hay.containsIgnoreCase(w)) ok = false;
            }
            if (ok) shown.push_back(r);
        }
        sortShown();
        table.updateContent();
        table.repaint();
        countLabel.setText(juce::String((int)shown.size()) + (shown.size() == all.size() ? juce::String(" tracks") : " of " + juce::String((int)all.size()) + " tracks"), juce::dontSendNotification);
        selectedRowsChanged(table.getSelectedRow());
    }

    const RowRef* selectedRow() const {
        const int r = table.getSelectedRow();
        return (r >= 0 && r < (int)shown.size()) ? &shown[(size_t)r] : nullptr;
    }

    void edited(int row, int colId, const juce::String& text) {
        if (row < 0 || row >= (int)shown.size() || colId < 1 || colId > (int)cols.size()) return;
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        const Column& c = cols[(size_t)colId - 1];
        juce::String v = text.trim();
        if (!c.custom && v == idx.get(shown[(size_t)row].row, c.base)) v = {}; //typing the measured value back = no override
        lib->setUserValue(shown[(size_t)row].id, c.key, v);
        reload();
    }

    void revealTrack(const RowRef& r) {
        const juce::File src(idx.get(r.row, "source_path"));
        if (src.existsAsFile()) src.revealToUser();
        else if (auto lib = getLibrary ? getLibrary() : nullptr) lib->folder.getChildFile("tracks").getChildFile(r.id + ".meta.json").revealToUser();
    }

    void showDetails(const RowRef& r) {
        juce::String t;
        bool y, u;
        for (auto& c : cols) {const auto v = display(r, c, y, u); if (v.isNotEmpty()) t << c.header << ":  " << v << (y ? "   (yours)" : "") << "\n";}
        t << "\nauthority / power / detail values:  " << idx.get(r.row, "authority_value") << " / " << idx.get(r.row, "power_value") << " / " << idx.get(r.row, "detail_value");
        t << "\nletter value:  " << idx.get(r.row, "letter_value") << "     bonus value:  " << idx.get(r.row, "bonus_value");
        t << "\nBPM estimate:  " << idx.get(r.row, "bpm_est") << "  (confidence " << idx.get(r.row, "bpm_confidence") << ")";
        t << "\nkey estimate:  " << idx.get(r.row, "key_est") << "  (confidence " << idx.get(r.row, "key_confidence") << ")";
        t << "\n" << idx.get(r.row, "sample_rate") << " Hz, " << idx.get(r.row, "bits") << " bit, " << idx.get(r.row, "channels") << " ch" << (idx.get(r.row, "lossy") == "yes" ? ", LOSSY" : "");
        t << "\ncapture version " << idx.get(r.row, "capture_version") << ",  id " << r.id;
        t << "\n" << idx.get(r.row, "source_path");
        auto* w = new juce::AlertWindow(idx.get(r.row, "file"), {}, juce::MessageBoxIconType::NoIcon, this);
        auto* ed = new juce::TextEditor();
        ed->setMultiLine(true); ed->setReadOnly(true); ed->setText(t); ed->setSize(560, 380);
        ed->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        w->addCustomComponent(ed);
        w->addButton("Show file", 1); w->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const RowRef copy = r;
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, copy, ed](int result) { if (result == 1) revealTrack(copy); delete ed; }), true);
    }

    void addColumnDialog() {
        auto* w = new juce::AlertWindow("Add a column", "A column of your own, for example Streams, Sales, Chart peak, Notes.", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", "", "Column name");
        w->addComboBox("type", {"Numbers (sorts as numbers)", "Text"}, "Holds");
        w->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
            if (result != 1) return;
            const auto name = w->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;
            if (auto lib = getLibrary()) {lib->addCustomColumn(name, w->getComboBoxComponent("type")->getSelectedItemIndex() == 0); reload();}
        }), true);
    }

    void columnsMenu() {
        juce::PopupMenu m;
        if (custom.empty()) m.addItem(-1, "No columns of yours yet (use Add column)", false);
        for (int i = 0; i < (int)custom.size(); ++i) {
            juce::PopupMenu sub;
            sub.addItem(1000 + i, "Rename...");
            sub.addItem(2000 + i, "Remove (deletes its values)");
            m.addSubMenu(custom[(size_t)i].name + (custom[(size_t)i].numeric ? "  (numbers)" : "  (text)"), sub);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&columnsButton), [this](int id) {
            if (id >= 1000 && id < 1000 + (int)custom.size()) renameDialog(custom[(size_t)id - 1000]);
            if (id >= 2000 && id < 2000 + (int)custom.size()) removeColumnDialog(custom[(size_t)id - 2000]);
        });
    }
    void renameDialog(const f8k::CustomColumn c) {
        auto* w = new juce::AlertWindow("Rename column", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", c.name, "New name");
        w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, c](int result) {
            const auto name = w->getTextEditorContents("name").trim();
            if (result == 1 && name.isNotEmpty()) if (auto lib = getLibrary()) {lib->renameCustomColumn(c.key, name); reload();}
        }), true);
    }
    void removeColumnDialog(const f8k::CustomColumn c) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Remove column", "Remove \"" + c.name + "\" and all the values in it?", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create([this, c](int result) { if (result == 1) if (auto lib = getLibrary()) {lib->removeCustomColumn(c.key); reload();} }));
    }
    void importDialog() {
        chooser = std::make_unique<juce::FileChooser>("Import values from a CSV (needs a \"file\" column)", juce::File(), "*.csv;*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            if (auto lib = getLibrary()) {
                const auto report = lib->importCsv(f);
                reload();
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Import", report, "OK", this);
            }
        });
    }
    void removeSelected() {
        const auto* r = selectedRow();
        if (r == nullptr) return;
        const juce::String id = r->id, name = idx.get(r->row, "file");
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Remove from library",
            "Remove \"" + name + "\" from the library? Its captured data and your values for it are deleted. The audio file itself is not touched.", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create([this, id](int result) { if (result == 1) if (auto lib = getLibrary()) {lib->removeTrack(id); reload();} }));
    }

    juce::TableListBox table;
    juce::TextEditor search;
    juce::TextButton addColumnButton{"Add column..."}, columnsButton{"Your columns..."}, importButton{"Import CSV..."}, showButton{"Show file"}, removeButton{"Remove..."};
    juce::Label countLabel;
    juce::Rectangle<int> hint;
    std::unique_ptr<juce::FileChooser> chooser;
    f8k::Table idx;
    std::map<juce::String, std::map<juce::String, juce::String>> user;
    std::vector<f8k::CustomColumn> custom;
    std::vector<Column> cols;
    juce::StringArray colKeys;
    std::vector<RowRef> all, shown;
    int sortCol = 0; bool sortForwards = true;
    int lastChange = -1; const void* lastLib = nullptr;
    bool restoringLayout = false;
    struct HeaderWatcher : public juce::TableHeaderComponent::Listener {
        LibraryView& v; explicit HeaderWatcher(LibraryView& o) : v(o) {}
        void tableColumnsChanged(juce::TableHeaderComponent*) override { v.columnsChangedOrResized(); }
        void tableColumnsResized(juce::TableHeaderComponent*) override { v.columnsChangedOrResized(); }
        void tableSortOrderChanged(juce::TableHeaderComponent*) override {}
    } headerWatcher{*this};
};

//=============================================================================================== the window
class MainComponent : public juce::Component {
public:
    explicit MainComponent(const juce::File& overrideFolder = {}) {
        juce::PropertiesFile::Options o; o.applicationName = "Meter F8K Analyser"; o.filenameSuffix = ".settings"; o.folderName = "Meter F8K";
        settings = std::make_unique<juce::PropertiesFile>(o);
        setFolder(overrideFolder != juce::File() ? overrideFolder : juce::File(settings->getValue("library", defaultLibrary().getFullPathName())));
        worker.log = [this](const juce::String& l) { addPanel.log(l); };
        worker.status = [this](int d, int t, double p, const juce::String& n) { addPanel.setStatus(d, t, p, n); };
        worker.startThread();
        addPanel.onFiles = [this](const juce::Array<juce::File>& f) { worker.add(f); };
        addPanel.onOpenFolder = [this] { folder.createDirectory(); folder.startAsProcess(); };
        addPanel.onChangeFolder = [this] { chooseLibrary(); };
        libraryView.getLibrary = [this] { return library; };
        tabs.addTab("Add tracks", kBg, &addPanel, false);
        tabs.addTab("Library", kBg, &libraryView, false);
        tabs.setColour(juce::TabbedComponent::outlineColourId, kBg);
        tabs.setColour(juce::TabbedButtonBar::tabTextColourId, kDim);
        tabs.setColour(juce::TabbedButtonBar::frontTextColourId, kText);
        tabs.setTabBarDepth(32);
        tabs.setCurrentTabIndex(0);
        addAndMakeVisible(tabs);
        setSize(1100, 680);
    }
    ~MainComponent() override { worker.signalThreadShouldExit(); worker.notify(); }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); }
    void resized() override { tabs.setBounds(getLocalBounds()); }
    //for automated checks: pictures of both tabs
    void snapshot(const juce::File& outDir) {
        outDir.createDirectory();
        for (int t = 0; t < 2; ++t) {
            tabs.setCurrentTabIndex(t);
            if (t == 1) libraryView.reload();
            const auto img = createComponentSnapshot(getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os(outDir.getChildFile(t == 0 ? "tab-add.png" : "tab-library.png"));
            if (os.openedOk()) {os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os);}
        }
    }
    std::shared_ptr<f8k::Library> getLibrary() const { return library; }
private:
    void setFolder(const juce::File& f) {
        folder = f;
        library = std::make_shared<f8k::Library>(folder);
        worker.setLibrary(library);
        addPanel.log("Library: " + folder.getFullPathName());
    }
    void chooseLibrary() {
        chooser = std::make_unique<juce::FileChooser>("Choose the library folder", folder);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            settings->setValue("library", f.getFullPathName()); settings->saveIfNeeded();
            setFolder(f);
        });
    }
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::File folder;
    std::shared_ptr<f8k::Library> library;
    AddPanel addPanel;
    LibraryView libraryView;
    Worker worker{*this};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> chooser;
};

class AnalyserApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Meter F8K Analyser"; }
    const juce::String getApplicationVersion() override { return "1.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String& commandLine) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::AlertWindow::backgroundColourId, kPanel);
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::AlertWindow::textColourId, kText);
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::PopupMenu::backgroundColourId, kPanel);
        juce::StringArray args; args.addTokens(commandLine, true); args.trim(); args.removeEmptyStrings();
        const int si = args.indexOf("--snapshot");
        if (si >= 0 && si + 2 < args.size()) { //--snapshot <library folder> <output folder>: draw both tabs to PNG files and quit (for automated checks)
            const juce::File lib(args[si+1].unquoted()), out(args[si+2].unquoted());
            snap = std::make_unique<MainComponent>(lib);
            if (auto l = snap->getLibrary()) { //show a column of yours too
                l->upgradeOldRows();
                if (l->customColumns().empty()) {
                    const auto key = l->addCustomColumn("Streams", true);
                    auto t = l->index();
                    for (size_t r = 0; r < t.rows.size(); ++r) l->setUserValue(t.get(r, "id"), key, juce::String(1000 * (int)(r + 1) * 37 % 9000));
                    if (!t.rows.empty()) l->setUserValue(t.get(0, "id"), "artist", "Edited Artist");
                }
            }
            juce::Timer::callAfterDelay(1500, [this, out] { snap->snapshot(out); snap.reset(); quit(); });
            return;
        }
        window = std::make_unique<Window>();
    }
    void shutdown() override { snap.reset(); window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    struct Window : public juce::DocumentWindow {
        Window() : juce::DocumentWindow("Meter F8K Analyser", kBg, juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            setResizeLimits(760, 460, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
    std::unique_ptr<MainComponent> snap;
};

} // namespace

START_JUCE_APPLICATION(AnalyserApp)
