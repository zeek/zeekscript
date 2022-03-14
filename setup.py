import setuptools.command.build_py

from setuptools import setup
from setuptools.dist import Distribution
from setuptools.command.install import install


class BinaryDistribution(Distribution):
    """Distribution which always forces a binary package with platform name"""
    # https://stackoverflow.com/a/62668026
    def has_ext_modules(self):
        return True

class InstallPlatlib(install):
    """Additional tweak to treat the package as platform-specific."""
    # https://github.com/google/or-tools/issues/616#issuecomment-371480314
    def finalize_options(self):
        install.finalize_options(self)
        self.install_lib = self.install_platlib

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
    version=open('VERSION').read().replace('-', '.dev', 1).strip(),
    description='A Zeek script formatter and analyzer',
    long_description=open('README.md').read(),
    long_description_content_type='text/markdown',
    keywords='zeek scripts language formatter formatting indenter indenting parsing',
    maintainer='The Zeek Project',
    maintainer_email='info@zeek.org',
    url='https://github.com/ckreibich/zeek-script',

    scripts=['zeek-format', 'zeek-script'],
    packages=['zeekscript'],
    package_data={'zeekscript': ['zeek-language.so']},

    cmdclass={
        'build_py': BuildCommand,
        'install': InstallPlatlib,
    },

    distclass=BinaryDistribution,

    setup_requires=['tree_sitter'],
    install_requires=['tree_sitter'],
    python_requires='>3.7.0',

    classifiers=[
        'Programming Language :: Python :: 3.7',
        'License :: OSI Approved :: BSD License',
        'Topic :: Utilities',
    ],
)
