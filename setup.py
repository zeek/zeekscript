import setuptools.command.build_py

from setuptools import setup
from setuptools.dist import Distribution


class BinaryDistribution(Distribution):
    """Distribution which always forces a binary package with platform name"""
    # https://stackoverflow.com/a/62668026
    def has_ext_modules(_):
        return True

class BuildCommand(setuptools.command.build_py.build_py):
    """A customized build command that also rebuilds the parser bindings as needed."""
    def run(self):
        self.refresh_bindings()
        super().run() # Run regular build procedure

    def refresh_bindings(self):
        try:
            import tree_sitter
        except ImportError:
            print('Warning: tree_sitter module not found, not refreshing bindings')
            return

        # Recompile the tree-sitter bindings. This is a no-op when the parser
        # needs no rebuild. We specify the library output path and the
        # tree-sitter repo path to include.
        tree_sitter.Language.build_library('./zeekscript/zeek-language.so',
                                           ['tree-sitter-zeek'])


setup(
    name='zeekscript',
    version='0.1.1',
    description='A Zeek script formatter and analyzer',
    maintainer='The Zeek Project',
    maintainer_email='info@zeek.org',
    url='https://github.com/ckreibich/zeek-script',

    scripts=['zeek-script'],
    packages=['zeekscript'],
    package_data={'zeekscript': ['zeek-language.so']},

    cmdclass={ 'build_py': BuildCommand },
    distclass=BinaryDistribution,

    setup_requires=['tree_sitter'],
    install_requires=['tree_sitter'],

    classifiers=[
        'Programming Language :: Python :: 3',
        'Topic :: Utilities',
    ],
)
