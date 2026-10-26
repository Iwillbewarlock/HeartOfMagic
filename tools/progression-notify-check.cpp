// progression-notify-check - offline check of ProgressionNotifyQueue.h (H1-d)
//
// The plugin's own hold-and-send rule, no game types: a notice is sent at once
// with nothing open, held while a scope is open, and the outermost close sends
// what was held in posting order - nested scopes, an early return, and a
// receiver that posts again or opens its own scope while notices go out.
// Prints one PASS/FAIL line per case; exit code 1 on any FAIL.

#include "ProgressionNotifyQueue.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
    using Queue = ProgressionNotifyQueue<std::string>;

    int g_fail = 0;

    void Check(const char* name, bool ok, const std::vector<std::string>& got)
    {
        std::string text;
        for (const auto& item : got) text += (text.empty() ? "" : ",") + item;
        std::printf("%s %s [%s]\n", ok ? "PASS" : "FAIL", name, text.c_str());
        if (!ok) ++g_fail;
    }

    // The Batch the plugin uses, over a test queue
    struct Scope
    {
        Queue& queue;
        std::vector<std::string>& sent;
        explicit Scope(Queue& q, std::vector<std::string>& s) : queue(q), sent(s) { queue.Open(); }
        ~Scope()
        {
            queue.Close([this](const std::string& e) { sent.push_back(e); });
        }
    };

    void NoScope()
    {
        Queue queue;
        std::vector<std::string> sent;
        queue.Post("a", [&](const std::string& e) { sent.push_back(e); });
        Check("no scope: sent at once", sent == std::vector<std::string>{ "a" } && queue.Held() == 0, sent);
    }

    void HeldUntilClose()
    {
        Queue queue;
        std::vector<std::string> sent;
        auto send = [&](const std::string& e) { sent.push_back(e); };
        bool emptyInside = false;
        {
            Scope scope(queue, sent);
            queue.Post("clear", send);
            queue.Post("set", send);
            emptyInside = sent.empty();
        }
        Check("one scope: held, then sent in order", emptyInside && sent == std::vector<std::string>{ "clear", "set" }, sent);
    }

    void Nested()
    {
        Queue queue;
        std::vector<std::string> sent;
        auto send = [&](const std::string& e) { sent.push_back(e); };
        bool emptyAfterInner = false;
        {
            Scope outer(queue, sent);
            queue.Post("drop", send);
            {
                Scope inner(queue, sent);   // SetLearningTarget -> DropTargetFromOtherSchools
                queue.Post("clear", send);
            }
            emptyAfterInner = sent.empty();
            queue.Post("unlocked", send);
        }
        Check("nested: inner close sends nothing, outer sends all in order",
            emptyAfterInner && sent == std::vector<std::string>{ "drop", "clear", "unlocked" } && queue.Depth() == 0, sent);
    }

    void EarlyReturn()
    {
        Queue queue;
        std::vector<std::string> sent;
        auto send = [&](const std::string& e) { sent.push_back(e); };
        auto step = [&](bool stop) {
            Scope scope(queue, sent);
            queue.Post("first", send);
            if (stop) return;
            queue.Post("second", send);
        };
        step(true);
        Check("early return: what was held still goes", sent == std::vector<std::string>{ "first" } && queue.Depth() == 0, sent);
    }

    void PostWhileSending()
    {
        Queue queue;
        std::vector<std::string> sent;
        std::function<void(const std::string&)> send = [&](const std::string& e) {
            sent.push_back(e);
            if (e == "a") queue.Post("a-reply", send);   // a receiver that changes state again
        };
        {
            queue.Open();
            queue.Post("a", send);
            queue.Post("b", send);
            queue.Close(send);
        }
        Check("posted while sending: after the held ones", sent == std::vector<std::string>{ "a", "b", "a-reply" }, sent);
    }

    void ScopeWhileSending()
    {
        Queue queue;
        std::vector<std::string> sent;
        std::function<void(const std::string&)> send = [&](const std::string& e) {
            sent.push_back(e);
            if (e == "a") {
                queue.Open();   // a receiver calling a function that opens its own Batch
                queue.Post("inner", send);
                queue.Close(send);
            }
        };
        queue.Open();
        queue.Post("a", send);
        queue.Post("b", send);
        queue.Close(send);
        Check("scope opened while sending: no early send, order kept",
            sent == std::vector<std::string>{ "a", "b", "inner" } && queue.Depth() == 0 && queue.Held() == 0, sent);
    }

    void ExtraClose()
    {
        Queue queue;
        std::vector<std::string> sent;
        auto send = [&](const std::string& e) { sent.push_back(e); };
        queue.Close(send);   // never below zero
        queue.Post("a", send);
        Check("close with nothing open: depth stays 0", queue.Depth() == 0 && sent == std::vector<std::string>{ "a" }, sent);
    }
}

int main()
{
    NoScope();
    HeldUntilClose();
    Nested();
    EarlyReturn();
    PostWhileSending();
    ScopeWhileSending();
    ExtraClose();
    std::printf("%s fail=%d\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
