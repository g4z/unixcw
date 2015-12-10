{
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


        Acceptance tests for CW sender program
        ======================================

Test the limits of the CW sender program, and verify its response under
error input conditions.

}{

Initialize the test to 30 WPM, 800Hz, 70% volume, no gaps, 50% weighting

}%C1;%P1;%M1;%E1;%O1;%G0;%K50;%T800;%V70;%W30;{

Demonstrate character set and combinations

}= {CHARSET     } ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"$()+-./:;=?_ ={
}= {MIXED CASE  } The lazy dog jumps over the quick brown fox ={
}= {COMBINATIONS} [AR] [VA] [CT] ={

Test the limits of WPM, gap, weighting, Hz, and volume

}%W4;= 4 WPM
%W60;= 60 WPM
%W30;={

}%G0;= GAP 0
%G60;= GAP 60
%G0;={

}%K20;= WEIGHTING 20
%K40;= WEIGHTING 40
%K60;= WEIGHTING 60
%K80;= WEIGHTING 80
%K50;={

}%T0;= TONE 0
%T10;= TONE 10
%T100;= TONE 100
%T1000;= TONE 1000
%T2000;= TONE 2000
%T4000;= TONE 4000
%T800;={

}%V0;= VOL 0
%V20;= VOL 20
%V40;= VOL 40
%V60;= VOL 60
%V80;= VOL 80
%V100;= VOL 100
%V70;={

Switch flags on and off

}%E0;= ECHO OFF
%?E%E1;= ECHO ON
%?E%E1;={

}%M0;= MESSAGES OFF
%?M%M1;= MESSAGES ON
%?M%M1;={

}%O0;= {COMBINATIONS OFF} [AR] [VA] [CT]
%?O%O1;= {COMBINATIONS ON} [AR] [VA] [CT]
%?O%O1;={

}%P0;= {COMMENTS OFF, SOUNDED}
%?P%P1;= {COMMENTS ON, NOT SOUNDED}
%?P%P1;={

Test the queries

}= {QUERIES   } %?W%?T%?V%?G%?K%?C%?E%?M%?O%?P ={
}= {CW QUERIES} %>W%>T%>V%>G%>K%>C%>E%>M%>O%>P ={

Tests for errors on input

}= {BAD CHARACTERS  } | \ # ={
}= {BAD COMMANDS    } %J %B %% %# %; %| ={
}= {BAD COMMAND ARGS} %W61;%W0;%W-10;%T-1;%T4001;%T-100;%V-1;%V101 ={
}= {BAD COMMAND ARGS} %G-1;%G61;%K19;%K81; ={
}= {BAD QUERIES     } %?J %?B %?% %?; %?| ={
}= {BAD CW QUERIES  } %>J %>B %>% %>; %>| ={

Check that we can quit

}= QUIT %Q THIS TEXT SHOULD NOT APPEAR
