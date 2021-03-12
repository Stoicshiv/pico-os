#pragma once

#include <Std/Span.hpp>
#include <Std/StringView.hpp>
#include <Std/Array.hpp>
#include <Std/Vector.hpp>

namespace Std {
    class StringBuilder;

    template<typename T>
    void format_arg(StringBuilder&, T value);

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
                    format_arg(builder, *reinterpret_cast<const Parameters*>(value));
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

    private:
        Vector<char> m_data;
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
}
