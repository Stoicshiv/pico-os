#pragma once

#include <Std/Span.hpp>
#include <Std/StringView.hpp>
#include <Std/Array.hpp>
#include <Std/Vector.hpp>
#include <Std/Concepts.hpp>
#include <Std/String.hpp>

namespace Std {
    class StringBuilder;
    class String;

    template<typename T>
    struct Formatter {
        using __no_formatter_defined = void;
    };

    template<typename T, typename = void>
    struct HasFormatter {
        static constexpr bool value = true;
    };
    template<typename T>
    struct HasFormatter<T, typename Formatter<T>::__no_formatter_defined> {
        static constexpr bool value = false;
    };

    using FormatFunction = void(*)(StringBuilder&, const void*);

    struct TypeErasedFormatParameter {
        const void *m_value;
        FormatFunction m_format;
    };

    class TypeErasedFormatParams {
    public:
        const TypeErasedFormatParameter& param(usize index) const { return m_params[index]; }

    protected:
        void set_params(Span<TypeErasedFormatParameter> params)
        {
            m_params = params;
        }

    private:
        Span<TypeErasedFormatParameter> m_params;
    };

    template<typename... Parameters>
    class VariadicFormatParams : public TypeErasedFormatParams {
    public:
        explicit VariadicFormatParams(const Parameters&... parameters)
            : m_params { TypeErasedFormatParameter {
                &parameters,
                [](StringBuilder& builder, const void *value)
                {
                    Formatter<Parameters>::format(builder, *reinterpret_cast<const Parameters*>(value));
                },
            }... }
        {
            this->set_params(m_params.span());
        }

    private:
        Array<TypeErasedFormatParameter, sizeof...(Parameters)> m_params;
    };

    void vformat(StringBuilder&, StringView fmtstr, TypeErasedFormatParams);

    class StringBuilder {
    public:
        void append(char value)
        {
            m_data.append(value);
        }
        void append(StringView value)
        {
            for (char ch : value.iter())
                m_data.append(ch);
        }
        template<typename... Parameters>
        void appendf(StringView fmtstr, const Parameters&... parameters)
        {
            vformat(*this, fmtstr, VariadicFormatParams { parameters... });
        }

        char* data() { return m_data.data(); }
        usize size() { return m_data.size(); }

        StringView view() const { return m_data.span(); }
        String string() const { return view(); }

    private:
        Vector<char, 256> m_data;
    };

    inline void vformat(StringBuilder& builder, StringView fmtstr, TypeErasedFormatParams params)
    {
        usize params_index = 0;
        for (char ch : fmtstr.iter())
        {
            if (ch == '%') {
                auto parameter = params.param(params_index++);
                parameter.m_format(builder, parameter.m_value);
            } else {
                builder.append(ch);
            }
        }
    }

    void dbgln_raw(StringView);

    template<typename... Parameters>
    void dbgln(StringView fmtstr, const Parameters&... parameters)
    {
        StringBuilder builder;
        vformat(builder, fmtstr, VariadicFormatParams { parameters... });
        dbgln_raw(builder.view());
    }

    template<typename... Parameters>
    void dbgln(const char *fmtstr, const Parameters&... parameters)
    {
        StringBuilder builder;
        vformat(builder, fmtstr, VariadicFormatParams { parameters... });
        dbgln_raw(builder.view());
    }

    template<typename T>
    requires Concepts::Integral<T>
    struct Formatter<T> {
        static void format(StringBuilder&, T);
    };

    template<typename T>
    struct Formatter<T*> {
        static void format(StringBuilder& builder, T *value)
        {
            static_assert(sizeof(T*) == 4);
            return Formatter<u32>::format(builder, reinterpret_cast<u32>(value));
        }
    };

    template<>
    struct Formatter<StringView> {
        static void format(StringBuilder&, StringView);
    };
    template<>
    struct Formatter<String> : Formatter<StringView> {
    };
    template<>
    struct Formatter<const char*> : Formatter<StringView> {
    };
    template<usize Size>
    struct Formatter<char[Size]> : Formatter<StringView> {
    };

    template<>
    struct Formatter<bool> {
        static void format(StringBuilder&, bool);
    };
    template<>
    struct Formatter<char> {
        static void format(StringBuilder&, char);
    };

    template<typename T>
    struct Formatter<Span<T>> {
        static void format(StringBuilder& builder, Span<T> value)
        {
            builder.appendf("{ %, % }", value.data(), value.size());
        }
    };

    template<>
    struct Formatter<Path> {
        static void format(StringBuilder&, const Path&);
    };

    static_assert(HasFormatter<int>::value == true);
    static_assert(HasFormatter<double>::value == false);
}

using Std::dbgln;
