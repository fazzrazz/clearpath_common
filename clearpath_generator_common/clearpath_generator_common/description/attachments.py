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

from clearpath_config.platform.attachments.config import BaseAttachment
from clearpath_config.platform.types.bumper import Bumper
from clearpath_config.platform.types.top_plate import TopPlate
from clearpath_config.platform.types.structure import Structure

from typing import List


class AttachmentsDescription():
    class BaseDescription():
        pkg_clearpath_platform_description = 'clearpath_platform_description'

        def __init__(self, platform: str, attachment: BaseAttachment) -> None:
            self.attachment = attachment
            self.package = self.pkg_clearpath_platform_description
            self.path = 'urdf/' + platform + '/attachments/'
            self.file = self.attachment.get_name()

            self.parameters = {}

        def get_parameters(self) -> dict:
            return self.parameters

        def get_parameter(self, parameter: str) -> str:
            return self.parameters[parameter]

        def get_package(self) -> str:
            return self.package

        def get_path(self) -> str:
            return self.path

        def get_file(self) -> str:
            return self.file

        def get_xyz(self) -> List[float]:
            return self.attachment.get_xyz()

        def get_rpy(self) -> List[float]:
            return self.attachment.get_rpy()

    class BumperDescription(BaseDescription):
        NAME = 'name'
        EXTENSION = 'extension'

        def __init__(self, platform: str, attachment: Bumper) -> None:
            super().__init__(platform, attachment)
            self.file = 'bumper'
            self.parameters.update({
                self.NAME: attachment.get_name(),
                self.EXTENSION: attachment.get_extension()
            })

    class TopPlateDescription(BaseDescription):
        MODEL = 'model'

        def __init__(self, platform: str, attachment: TopPlate) -> None:
            super().__init__(platform, attachment)
            self.parameters.update({
                self.MODEL: attachment.get_model(),
            })

    class StructureDescription(BaseDescription):
        MODEL = 'model'
        PARENT_LINK = 'parent_link'

        def __init__(self, platform: str, attachment: Structure) -> None:
            super().__init__(platform, attachment)
            self.parameters.update({
                self.PARENT_LINK: 'top_plate_link',
                self.MODEL: attachment.get_model()
            })

    MODEL = {
        Bumper.ATTACHMENT_MODEL: BumperDescription,
        TopPlate.ATTACHMENT_MODEL: TopPlateDescription,
        Structure.ATTACHMENT_MODEL: StructureDescription
    }

    def __new__(cls, platform, attachment: BaseAttachment) -> BaseDescription:
        return AttachmentsDescription.MODEL[attachment.ATTACHMENT_MODEL](platform, attachment)
