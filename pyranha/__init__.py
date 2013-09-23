# -*- coding: iso-8859-1 -*-
# Copyright (C) 2009-2011 by Francesco Biscani
# bluescarni@gmail.com
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the
# Free Software Foundation, Inc.,
# 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

"""Root Pyranha module.

.. moduleauthor:: Francesco Biscani <bluescarni@gmail.com>

"""

from __future__ import absolute_import as _ai

__all__ = ['celmec', 'math', 'test', 'settings']

from ._common import _register_evaluate_wrapper, _register_repr_png, _register_repr_latex
import threading as _thr

def get_series(s):
	# TODO: check input, docs, catch error on return and provide better error message.
	from ._core import _get_series_list as gsl
	# Get the series list as a dictionary.
	sd = dict(gsl())
	return getattr(_core,'_series_'+str(sd[s]))

# TODO: evaluate the role of these things. Might need to protect with a mutex for consistency.
#for n in _series_types:
#	_register_evaluate_wrapper(n)
#	_register_repr_png(n)
#	_register_repr_latex(n)
#del n

class _settings(object):
	# Main lock for protecting reads/writes from multiple threads.
	__lock = _thr.RLock()
	# Wrapper to get/set max term output.
	@property
	def max_term_output(self):
		from ._core import _settings as _s
		return _s._get_max_term_output()
	@max_term_output.setter
	def max_term_output(self,n):
		from ._core import _settings as _s
		_s._set_max_term_output(n)
	# Wrapper method to enable/disable latex representation.
	@property
	def latex_repr(self):
		import re
		from . import _core
		with self.__lock:
			s_names = list(filter(lambda s: re.match('\_' + _series_types[0] + '\_\d+',s),dir(_core)))
			return hasattr(getattr(_core,s_names[0]),'_repr_latex_')
	@latex_repr.setter
	def latex_repr(self,flag):
		import re
		from . import _core
		f = bool(flag)
		with self.__lock:
			if f == self.latex_repr:
				return
			if f:
				for n in _series_types:
					_register_repr_latex(n)
			else:
				for n in _series_types:
					s_names = list(filter(lambda s: re.match('\_' + n + '\_\d+',s),dir(_core)))
					for s in s_names:
						delattr(getattr(_core,s),'_repr_latex_')

settings = _settings()
