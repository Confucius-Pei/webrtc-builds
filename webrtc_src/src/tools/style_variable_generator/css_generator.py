# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from style_variable_generator.base_generator import Color, Modes, BaseGenerator, VariableType
import collections


class CSSStyleGenerator(BaseGenerator):
    '''Generator for CSS Variables'''

    @staticmethod
    def GetName():
        return 'CSS'

    def Render(self):
        self.Validate()
        return self.ApplyTemplate(self, 'css_generator.tmpl',
                                  self.GetParameters())

    def GetParameters(self):
        if self.generate_single_mode:
            resolved_colors = self.model[VariableType.COLOR].Flatten(
                resolve_missing=True)
            resolved_opacities = self.model[VariableType.OPACITY].Flatten(
                resolve_missing=True)
            colors = {
                Modes.DEFAULT: resolved_colors[self.generate_single_mode]
            }
            opacities = {
                Modes.DEFAULT: resolved_opacities[self.generate_single_mode]
            }
        else:
            colors = self.model[VariableType.COLOR].Flatten()
            opacities = self.model[VariableType.OPACITY].Flatten()

        return {
            'opacities': opacities,
            'colors': colors,
        }

    def GetFilters(self):
        return {
            'to_css_var_name': self.ToCSSVarName,
            'css_color': self._CSSColor,
            'css_opacity': self._CSSOpacity,
            'css_color_rgb': self.CSSColorRGB,
        }

    def GetGlobals(self):
        return {
            'css_color_var':
            self.CSSColorVar,
            'in_files':
            sorted(self.in_file_to_context.keys()),
            'dark_mode_selector':
            self.generator_options.get('dark_mode_selector', None),
            'debug_placeholder':
            self.generator_options.get('debug_placeholder', ''),
            'Modes':
            Modes,
        }

    def GetCSSVarNames(self):
        '''Returns generated CSS variable names (excluding the rgb versions)'''
        names = set()
        for name in self.model[VariableType.COLOR].keys():
            names.add(self.ToCSSVarName(name))

        return names

    def _GetCSSVarPrefix(self, model_name):
        prefix = self.context_map[model_name].get(CSSStyleGenerator.GetName(),
                                                  {}).get('prefix')
        return prefix + '-' if prefix else ''

    def ToCSSVarName(self, model_name):
        return '--%s%s' % (self._GetCSSVarPrefix(model_name),
                           model_name.replace('_', '-'))

    def _CSSOpacity(self, opacity):
        if opacity.var:
            return 'var(%s)' % self.ToCSSVarName(opacity.var)

        return ('%f' % opacity.a).rstrip('0').rstrip('.')

    def _CSSColor(self, c):
        '''Returns the CSS color representation of |c|'''
        assert (isinstance(c, Color))
        if c.var:
            return 'var(%s)' % self.ToCSSVarName(c.var)

        if c.rgb_var:
            if c.opacity.a != 1:
                return 'rgba(var(%s-rgb), %g)' % (self.ToCSSVarName(
                    c.RGBVarToVar()), self._CSSOpacity(c.opacity))
            else:
                return 'rgb(var(%s-rgb))' % self.ToCSSVarName(c.RGBVarToVar())

        elif c.a != 1:
            return 'rgba(%d, %d, %d, %g)' % (c.r, c.g, c.b,
                                             self._CSSOpacity(c.opacity))
        else:
            return 'rgb(%d, %d, %d)' % (c.r, c.g, c.b)

    def CSSColorRGB(self, c):
        '''Returns the CSS rgb representation of |c|'''
        if c.var:
            return 'var(%s-rgb)' % self.ToCSSVarName(c.var)

        if c.rgb_var:
            return 'var(%s-rgb)' % self.ToCSSVarName(c.RGBVarToVar())

        return '%d, %d, %d' % (c.r, c.g, c.b)

    def CSSColorVar(self, model_name, color):
        '''Returns the CSS color representation given a color name and color'''
        if color.var:
            return 'var(%s)' % self.ToCSSVarName(color.var)
        if color.opacity and color.opacity.a != 1:
            return 'rgba(var(%s-rgb), %s)' % (self.ToCSSVarName(model_name),
                                              self._CSSOpacity(color.opacity))
        else:
            return 'rgb(var(%s-rgb))' % self.ToCSSVarName(model_name)
