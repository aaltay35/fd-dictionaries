#!/usr/bin/perl
# 

# Copyright (C) 2000 Horst Eyermann <horst@freedict.de>
#  
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software 
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

use XML::ESISParser;
use XML::Handler::Sample;

use teihandler;


push (@additional_args, IsSGML => 1);

$file = shift @ARGV;

$input = $file ;

die "Can�t finde file \"$input\"" unless -f $input;


$my_handler = XML::Handler::Sample->new;

teihandler->set_file_name($file);

XML::ESISParser->new->parse(Source => { SystemId => $input },
                            Handler => teihandler->new,
                            @additional_args);


