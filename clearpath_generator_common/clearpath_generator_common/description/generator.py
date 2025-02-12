#!/usr/bin/env python3

# Software License Agreement (BSD)
#
# @author    Roni Kreinin <rkreinin@clearpathrobotics.com>
# @copyright (c) 2023, Clearpath Robotics, Inc., All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# * Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of Clearpath Robotics nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Redistribution and use in source and binary forms, with or without
# modification, is not permitted without the express permission
# of Clearpath Robotics.

from clearpath_generator_common.common import BaseGenerator
from clearpath_generator_common.description.writer import XacroWriter
from clearpath_generator_common.description.mounts import MountDescription
from clearpath_generator_common.description.platform import PlatformDescription
from clearpath_generator_common.description.links import LinkDescription
from clearpath_generator_common.description.attachments import AttachmentsDescription
from clearpath_generator_common.description.sensors import SensorDescription


class DescriptionGenerator(BaseGenerator):
    def __init__(self,
                 setup_path: str = '/etc/clearpath/') -> None:
        super().__init__(setup_path)
        self.xacro_writer = XacroWriter(self.setup_path, self.serial_number)

    def generate(self) -> None:
        # Common macros
        self.xacro_writer.write_include(
            package=self.pkg_clearpath_platform_description.get_name(),
            file='common',
            path='urdf/')

        # Platform
        self.generate_platform()
        self.xacro_writer.write_newline()

        # Attachments
        self.generate_attachments()
        self.xacro_writer.write_newline()

        # Links
        self.generate_links()
        self.xacro_writer.write_newline()

        # Mounts
        self.generate_mounts()
        self.xacro_writer.write_newline()

        # Sensors
        self.generate_sensors()
        self.xacro_writer.write_newline()

        # Extras
        self.generate_extras()
        self.xacro_writer.write_newline()

        self.xacro_writer.close_file()

    def generate_platform(self) -> None:
        self.platform = self.clearpath_config.platform.get_model()
        platform_description = PlatformDescription(self.platform)

        # Platform macro
        self.xacro_writer.write_comment('Platform')
        self.xacro_writer.write_include(
            package=platform_description.get_package(),
            file=platform_description.get_file(),
            path=platform_description.get_path())
        self.xacro_writer.write_macro(platform_description.get_macro())

    def generate_attachments(self) -> None:
        self.xacro_writer.write_comment('Attachments')
        self.xacro_writer.write_newline()
        attachments = self.clearpath_config.platform.attachments.get_all()

        for attachment in attachments:
            if attachment.get_enabled():
                attachment_description = AttachmentsDescription(self.platform, attachment)
                self.xacro_writer.write_include(
                    package=attachment_description.get_package(),
                    file=attachment_description.get_file(),
                    path=attachment_description.get_path())

                self.xacro_writer.write_macro(
                    macro=attachment_description.get_file(),
                    parameters=attachment_description.get_parameters(),
                    blocks=XacroWriter.add_origin(
                        attachment_description.get_xyz(),
                        attachment_description.get_rpy()))
                self.xacro_writer.write_newline()

    def generate_links(self) -> None:
        self.xacro_writer.write_comment('Links')
        self.xacro_writer.write_newline()
        links = self.clearpath_config.links.get_all_links()

        for link in links:
            link_description = LinkDescription(link)
            self.xacro_writer.write_include(
                package=link_description.get_package(),
                file=link_description.get_file(),
                path=link_description.get_path())

            self.xacro_writer.write_macro(
                macro=link_description.get_file(),
                parameters=link_description.get_parameters(),
                blocks=XacroWriter.add_origin(
                    link_description.get_xyz(),
                    link_description.get_rpy()))
            self.xacro_writer.write_newline()

    def generate_mounts(self) -> None:
        self.xacro_writer.write_comment('Mounts')
        self.xacro_writer.write_newline()
        mounts = self.clearpath_config.mounts.get_all_mounts()
        for mount in mounts:
            mount_description = MountDescription(mount)

            self.xacro_writer.write_comment(
                '{0}'.format(mount_description.get_name())
            )

            self.xacro_writer.write_include(
                package=mount_description.get_package(),
                file=mount_description.get_model(),
                path=mount_description.get_path()
            )

            self.xacro_writer.write_macro(
                macro='{0}'.format(mount_description.get_model()),
                parameters=mount_description.get_parameters(),
                blocks=XacroWriter.add_origin(
                    mount_description.get_xyz(), mount_description.get_rpy())
            )

            self.xacro_writer.write_newline()

    def generate_sensors(self) -> None:
        self.xacro_writer.write_comment('Sensors')
        self.xacro_writer.write_newline()
        sensors = self.clearpath_config.sensors.get_all_sensors()
        for sensor in sensors:
            sensor_description = SensorDescription(sensor)

            self.xacro_writer.write_comment(
                '{0}'.format(sensor_description.get_name())
            )

            self.xacro_writer.write_include(
                package=sensor_description.get_package(),
                file=sensor_description.get_model(),
                path=sensor_description.get_path()
            )

            self.xacro_writer.write_macro(
                macro='{0}'.format(sensor_description.get_model()),
                parameters=sensor_description.get_parameters(),
                blocks=XacroWriter.add_origin(
                    sensor_description.get_xyz(), sensor_description.get_rpy())
            )

            self.xacro_writer.write_newline()

    def generate_extras(self) -> None:
        self.xacro_writer.write_comment('Extras')
        self.xacro_writer.write_newline()
        urdf_extras = self.clearpath_config.platform.extras.urdf
        if urdf_extras:
            self.xacro_writer.write_include(file=urdf_extras)
