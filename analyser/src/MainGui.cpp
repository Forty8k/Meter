// Meter F8K Analyser: the window app. Drop audio files or folders on it and each track is added to the library.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine.h"

namespace {

const juce::Colour kBg(24, 27, 33), kPanel(34, 38, 46), kText(220, 224, 232), kDim(140, 146, 158), kAccent(255, 225, 40);

juce::File defaultLibrary() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Meter F8K Library"); }

class Worker : public juce::Thread {
public:
    std::function<void(const juce::String&)> log;       //called on the message thread
    std::function<void(int, int, double, const juce::String&)> status; //done, total, current progress, current file
    explicit Worker(juce::Component& ownerComp) : juce::Thread("analyser"), owner(&ownerComp) {}
    ~Worker() override { stopThread(4000); }
    void setLibrary(const juce::File& f) { const juce::ScopedLock sl(lock); libraryFolder = f; library.reset(); }
    void add(const juce::Array<juce::File>& files) {
        { const juce::ScopedLock sl(lock); for (auto& f : files) queue.add(f); total += files.size(); }
        notify();
    }
    void run() override {
        while (!threadShouldExit()) {
            juce::File next;
            {
                const juce::ScopedLock sl(lock);
                if (queue.size() > 0) {next = queue.removeAndReturn(0);}
                if (next != juce::File() && library == nullptr) library = std::make_unique<f8k::Library>(libraryFolder);
            }
            if (next == juce::File()) { post(done, total, 0.0, {}); wait(-1); continue; }
            const juce::String name = next.getFileName();
            post(done, total, 0.0, name);
            const auto r = library->analyse(next, [this, name](double p) { post(done, total, p, name); }, [this] { return threadShouldExit(); });
            ++done;
            const juce::String line = (r.status == f8k::AnalyseOutcome::Added ? "added      " : r.status == f8k::AnalyseOutcome::AlreadyThere ? "skipped    " : "NOT ADDED  ")
                                      + name + "   (" + r.message + ")";
            juce::MessageManager::callAsync([this, sp = owner, line] { if (sp != nullptr && log) log(line); });
        }
    }
private:
    void post(int d, int t, double p, const juce::String& n) {
        juce::MessageManager::callAsync([this, sp = owner, d, t, p, n] { if (sp != nullptr && status) status(d, t, p, n); });
    }
    juce::CriticalSection lock;
    juce::Array<juce::File> queue;
    juce::File libraryFolder;
    std::unique_ptr<f8k::Library> library;
    std::atomic<int> done{0}, total{0};
    juce::Component::SafePointer<juce::Component> owner; //the worker lives inside its owner, so a live owner means a live worker
};

class MainComponent : public juce::Component, public juce::FileDragAndDropTarget {
public:
    MainComponent() {
        juce::PropertiesFile::Options o; o.applicationName = "Meter F8K Analyser"; o.filenameSuffix = ".settings"; o.folderName = "Meter F8K";
        settings = std::make_unique<juce::PropertiesFile>(o);
        libraryFolder = juce::File(settings->getValue("library", defaultLibrary().getFullPathName()));
        worker.setLibrary(libraryFolder);
        worker.log = [this](const juce::String& l) { logBox.moveCaretToEnd(); logBox.insertTextAtCaret(l + "\n"); };
        worker.status = [this](int d, int t, double p, const juce::String& n) {
            done = d; total = t; progress = t > 0 ? juce::jlimit(0.0, 1.0, (d + p) / (double)t) : 0.0;
            current = n; repaint();
        };
        worker.startThread();

        for (auto* b : {&addButton, &openButton, &changeButton}) {
            addAndMakeVisible(b);
            b->setColour(juce::TextButton::buttonColourId, kPanel);
            b->setColour(juce::TextButton::textColourOffId, kText);
        }
        addButton.onClick = [this] { chooseFiles(); };
        openButton.onClick = [this] { libraryFolder.createDirectory(); libraryFolder.startAsProcess(); };
        changeButton.onClick = [this] { chooseLibrary(); };
        addAndMakeVisible(progressBar);
        progressBar.setColour(juce::ProgressBar::foregroundColourId, kAccent.withAlpha(0.8f));
        progressBar.setColour(juce::ProgressBar::backgroundColourId, kPanel);
        addAndMakeVisible(logBox);
        logBox.setMultiLine(true); logBox.setReadOnly(true); logBox.setCaretVisible(false); logBox.setScrollbarsShown(true);
        logBox.setColour(juce::TextEditor::backgroundColourId, kPanel);
        logBox.setColour(juce::TextEditor::textColourId, kText);
        logBox.setColour(juce::TextEditor::outlineColourId, kPanel);
        logBox.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        logBox.setText("Library: " + libraryFolder.getFullPathName() + "\n");
        setSize(760, 560);
    }
    ~MainComponent() override { worker.signalThreadShouldExit(); worker.notify(); }

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
        g.drawText("Drop audio files or folders here", drop.withTrimmedBottom(drop.getHeight()/3), juce::Justification::centredBottom);
        g.setColour(kDim);
        g.setFont(juce::Font(14.0f));
        g.drawText("WAV, AIFF, FLAC (lossless is best). Each track is captured once and added to your library.",
                   drop.withTrimmedTop(drop.getHeight()*2/3 - 10), juce::Justification::centredTop);
        g.setColour(kText);
        g.setFont(juce::Font(14.0f));
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
        r.removeFromTop(22); //status line
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
        queueFiles(list, files.size());
    }
private:
    juce::Rectangle<int> dropArea() const { return getLocalBounds().reduced(16).removeFromTop(170); }
    juce::Rectangle<int> statusArea() const { auto r = getLocalBounds().reduced(16); r.removeFromTop(170 + 12 + 30 + 10); return r.removeFromTop(22); }
    void queueFiles(const juce::Array<juce::File>& list, int dropped) {
        if (list.isEmpty()) { logBox.moveCaretToEnd(); logBox.insertTextAtCaret("No audio files found in what was dropped (" + juce::String(dropped) + " item(s))\n"); return; }
        logBox.moveCaretToEnd(); logBox.insertTextAtCaret("Queued " + juce::String(list.size()) + " file(s)\n");
        worker.add(list);
    }
    void chooseFiles() {
        chooser = std::make_unique<juce::FileChooser>("Add audio files", juce::File(), "*.wav;*.wave;*.flac;*.aif;*.aiff;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) { juce::Array<juce::File> list; for (auto& f : fc.getResults()) f8k::Library::collectAudioFiles(f, list); if (fc.getResults().size()) queueFiles(list, fc.getResults().size()); });
    }
    void chooseLibrary() {
        chooser = std::make_unique<juce::FileChooser>("Choose the library folder", libraryFolder);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            libraryFolder = f; settings->setValue("library", f.getFullPathName()); settings->saveIfNeeded();
            worker.setLibrary(f);
            logBox.moveCaretToEnd(); logBox.insertTextAtCaret("Library: " + f.getFullPathName() + "\n");
        });
    }

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::File libraryFolder;
    Worker worker{*this};
    juce::TextButton addButton{"Add files..."}, openButton{"Open library folder"}, changeButton{"Change library folder..."};
    double progress = 0.0;
    juce::ProgressBar progressBar{progress};
    juce::TextEditor logBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int done = 0, total = 0;
    juce::String current;
    bool dragOver = false;
};

class AnalyserApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Meter F8K Analyser"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    struct Window : public juce::DocumentWindow {
        Window() : juce::DocumentWindow("Meter F8K Analyser", kBg, juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            setResizeLimits(600, 420, 2000, 1600);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};

} // namespace

START_JUCE_APPLICATION(AnalyserApp)
