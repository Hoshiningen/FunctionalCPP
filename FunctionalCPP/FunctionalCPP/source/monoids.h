#pragma once

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <functional>
#include <future>
#include <numeric>
#include <string>
#include <optional>
#include <vector>
#include <thread>
#include <list>
#include <execution>
#include <fstream>
#include <type_traits>

/*
    Monoids are defined by the laws that classify them. There are three that
    make something a monoid:

    1. They have to have totality.              f : X -> Y (where every Y is a valid value)
    2. They have to be associative.             (A + B) + C = A + (B + C)
    3. There has to be an identity element.     e + A = A + e = A

    A monoid has a type, a combining function (a binary operation), and an initial value (the identity element)
*/

namespace Monoids
{
/*
    Topics to study:
            
        1. Accumulation and Fold Expressions
            a. Left and Right Folding using accumulate
            b. applications of left and right folding
        2. Interesting monoid applications
        3. Expensive -> Cheap monoids
        4. Converting non-monoids into monoids (Map->Reduce)
*/

class AccumulateExperiments
{
public:

    static void Go()
    {
        PrintingWithAccumulate();
        OptionalReduction();
        FunctionComposition();
        MapReduce();
        Parallelization();
    }

private:

    /*
        An abstraction to ease the accumulate syntax on the eyes
    */
    template<typename Container, typename Value, typename BinaryOp>
    static auto LeftFold(const Container& container, Value&& init, BinaryOp&& combine)
    {
        return std::accumulate(std::cbegin(container), std::cend(container),
            std::forward<Value>(init), std::forward<BinaryOp>(combine));
    }

    /*
        Creates a task that runs based on the given policy. This is a helper function
        to ease making async tasks from arbitrary callable objects.
    */
    template<typename Fn, typename...Args>
    static auto CreateTask(const std::launch policy, Fn&& fn, Args&&...args)
    {
        return std::future<std::invoke_result_t<Fn, Args...>>
            { std::async(policy, std::forward<Fn>(fn), std::forward<Args>(args)...)};
    }

    /*
        A divide and conquer algorithm that recursively subdivides the range [begin, end)
        until it hits a load factor, then reduces each sub problem on the way out of recursion.
        The sums are computed sequentially, but as we work our way out of recursion, we
        reduce each side of the split asynchronously.

        There are some things that can be done to improve the algorithm:

        1. Change how the load factor is determined, as std::distance produces
            different performance depending on the passed in iterator types.

        2. Mimic c++17 execution policies, which would allow the reduction of a container
            that requires each reduction process to not be interleaved

        3. Investigate the inconsistent performance. It performs as expected,
            but could be optimized to perform better. (I believe this may be due
            to using task-based programming. If we could control the threads, then
            we may be able to even out the overall performance)
    */
    template<typename Iterator, typename Value, typename BinaryOp>
    static auto Reduce(Iterator begin, Iterator end, Value&& init, BinaryOp&& combine)
    {
        // Obtains the distance between begin and end in the first call to reduce
        static std::ptrdiff_t load = std::distance(begin, end) / std::thread::hardware_concurrency();

        // Constant with random access iterators, linear otherwise
        if (std::distance(begin, end) <= load)
            return std::accumulate(begin, end, std::forward<Value>(init), std::forward<BinaryOp>(combine));

        // Recursively reduce the left and right hand sides asynchronously. If this isn't done
        // in separate threads, then the algorithm is sequential.
        auto middle = std::next(begin, std::distance(begin, end) / 2);
        auto lhsTask = CreateTask(std::launch::async, Reduce<Iterator, Value, BinaryOp>,
            begin, middle, std::forward<Value>(init), std::forward<BinaryOp>(combine));

        auto rhsTask = CreateTask(std::launch::async, Reduce<Iterator, Value, BinaryOp>,
            middle, end, std::forward<Value>(init), std::forward<BinaryOp>(combine));

        // Left fold the results, but they could really be combined in any way.
        // This is an out of order reduction, and hence isn't a left or right fold.
        return combine(combine(init, lhsTask.get()), rhsTask.get());
    }

    class Timer
    {
    public:

        void Start()
        {
            previousTime = std::chrono::high_resolution_clock::now();
        }

        long long GetElapsed()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - previousTime).count();
        }

    private:

        std::chrono::high_resolution_clock::time_point previousTime{};
    };

private:

    /*
        This demonstrates using left-folding to print the contents
        of a string to standard output.
    */
    static void PrintingWithAccumulate()
    {
        std::string str = "Printing with accumulate!\n";

        auto combine = [](const auto& init, const char& item) {  return std::ref(init.get() << item); };
        LeftFold(str, std::ref(std::cout), combine);
    }

    /*
        This is my implementation of combining optional values. It really just checks
        the internals and performs a string concatenation of the values if they're
        present in the optionals.
    */
    static void OptionalReduction()
    {
        std::vector<std::optional<std::string>> optStrings = 
        {
            std::nullopt,
            std::make_optional("Printing "),
            std::nullopt,
            std::nullopt,
            std::make_optional("with "),
            std::make_optional("accumulate "),
            std::make_optional("through "),
            std::nullopt,
            std::make_optional("optionals!\n"),
            std::nullopt
        };

        std::optional<std::string> init = std::nullopt;

        auto combine = [](const std::optional<std::string>& init, const std::optional<std::string>& item)
        {
            if (init && !item)
                return std::make_optional(init.value());
            else if (!init && item)
                return std::make_optional(item.value());
            else if (init && item)
                return std::make_optional(init.value() + item.value());
            else
                return std::optional<std::string>{};
        };

        auto result = LeftFold(optStrings, init, combine);
        std::cout << result.value();
    }

    /*
        This demonstrates functions as monoids, where combining them is
        just a composition of functions.
    */
    static void FunctionComposition()
    {    
        std::vector<std::function<int(int)>> transformations =
        {
            [](const int item) { return 2 * item; },
            [](const int item) { return item + 4; },
            [](const int item) { return item / 6; },
            [](const int item) { return item - 7; },
        };

        std::function<int(int)> init = [](const int value) { return value; };

        auto combine = [](const auto& init, const auto& item)
        {
            return [init = init, item = item](const int value) { return item(init(value)); };
        };

        auto BigTransformation = LeftFold(transformations, init, combine);

        std::cout << BigTransformation(25) << "\n";
    }

    /*
        The classic map->reduce idiom. The purpose of it is to convert non-monoids into monoids
        so that you can aggregate them. This contrived example is arguable that the struct
        is reducable since the data they contain are both monoids, but it doesn't have any meaning
        without some context, which is what the mapping gives.
    */
    static void MapReduce()
    {
        struct NonMonoid
        {
            std::string name;
            int age;
        };

        std::vector<NonMonoid> vecNonMonoids
        {
            {"Sam", 25},
            {"Jaina", 107},
            {"Michelle", 23},
            {"Bob", 15},
            {"Lacy", 11},
            {"Margret", 22},
            {"Dave", 24},
            {"Louis", 31},
        };

        std::vector<int> vecMappedMonoids{};

        // Map the values into monoids that we can fold
        std::transform(std::begin(vecNonMonoids), std::end(vecNonMonoids), std::back_inserter(vecMappedMonoids),
            [](const auto& value)
            {
                if (value.age < 30 && value.age >= 15)
                    return 1;

                return 0;
            });

        // Reduce them to determine the number of entries that meet the given criteria
        auto EntriesBetween16And21 = LeftFold(vecMappedMonoids, 0, std::plus<int>{});
        std::cout << "Number of non-monoids that meet the given criteria: " << EntriesBetween16And21 << "\n";
    }

    /*
        During aggregation of user-defined monoids, the combining function has to take in
        monoids as a parameter. I'm curious at how many temporaries are created when reducing
        a container of these types of monoids, and what is needed to minimize that count.
    */
    static void AvoidingTemporaries()
    {
        /*
            The best way to avoid temporaries is to just take advantage of standard
            argument passing practices.

            1. Don't pass by value.
            2. If templated, pass by universal reference and forward it where appropriate.
            3. Pass by reference

            For example:
            std::accumulate(std::begin(), std::end(), init, combine);

            Given that syntax of accumulate, the only things we can control are init and combine.
                1. If you pass in an rvalue into init, then it will be moved in instead of copied.
                2. Combine can be any callable object, and if it's something that has a lifetime associated with it,
                   like a lambda or function object, then you can also forward that to prevent a copy from occurring.

            Ultimately, we can only save 2 copies from happening by not passing init and combine by value.

            This reiterates the guideline to not pass by value for expensive objects, and in the case of
            stl algorithms, we can't control what happens behind the scene. We can only control what we
            explicitly supply the method, and pass that by reference or rvalue. This excludes iterators.
        */
    }

    /*
        This is an experiment at parallelizing the reduction process, since monoids
        are trivially parallelizable. The point is to show that we can reduce, not left or right fold,
        a container of monoids, since they are associative and have an identity.
    */
    static void Parallelization()
    {
        Timer timer{};

        std::vector<double> values{};
        values.reserve(10'000'000);

        for (auto i = 0; i < values.capacity(); ++i)
            values.emplace_back(1.0);

        std::ofstream logger{ "D:\\execution_times.csv", std::ios::out };
        logger << "Iteration,Custom Reduce,C++17 Reduce,Sequential Reduce\n";

        for (auto i = 0; i < 1000; ++i)
        {
            logger << i + 1 << ",";
        
            // The custom-rolled reduce. This performs as we'd expect, which is to be significantly faster
            // than the sequential version. It falls behind the standard implementation of reduce, however.
            timer.Start();
            auto a = Reduce(std::begin(values), std::end(values), 0.0, std::plus<>());
            logger << timer.GetElapsed() << ",";
            std::cout << a;
        
            // A baseline for asynchronous reduction. If you have Cpp17, use this instead of a custom-rolled version.
            timer.Start();
            auto b = std::reduce(std::execution::par_unseq, std::begin(values), std::end(values), 0.0, std::plus<>());
            logger << timer.GetElapsed() << ",";
            std::cout << b;
        
            // A baseline for sequential reduction.
            timer.Start();
            auto c = std::accumulate(std::begin(values), std::end(values), 0.0, std::plus<>());
            logger << timer.GetElapsed() << "\n";
            std::cout << c;
        }
    }
};

}