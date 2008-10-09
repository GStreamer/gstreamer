<?xml version="1.0" encoding="utf-8"?>
<!--
#
# Copyright (C) 2006 Daniel Berrange
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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">

  <xsl:output method="html"/>

  <xsl:template match="coverage">
    <html>
      <head>
        <title>Coverage report</title>
        <style type="text/css">
          tbody tr.odd td.label {
            border-top: 1px solid rgb(128,128,128);
            border-bottom: 1px solid rgb(128,128,128);
          }
          tbody tr.odd td.label {
            background: rgb(200,200,200);
          }
          
          thead, tfoot {
            background: rgb(60,60,60);
            color: white;
            font-weight: bold;
          }

          tr td.perfect {
            background: rgb(0,255,0);
            color: black;
          }
          tr td.excellant {
            background: rgb(140,255,140);
            color: black;
          }
          tr td.good {
            background: rgb(160,255,0);
            color: black;
          }
          tr td.poor {
            background: rgb(255,160,0);
            color: black;
          }
          tr td.bad {
            background: rgb(255,140,140);
            color: black;
          }
          tr td.terrible {
            background: rgb(255,0,0);
            color: black;
          }
        </style>
      </head>
      <body>
        <h1>Coverage report</h1>
        <xsl:apply-templates/>
      </body>
    </html>
  </xsl:template>

  <xsl:template match="functions">
    <h2>Function coverage</h2>
    <xsl:call-template name="content">
      <xsl:with-param name="type" select="'function'"/>
    </xsl:call-template>
  </xsl:template>
  

  <xsl:template match="files">
    <h2>File coverage</h2>
    <xsl:call-template name="content">
      <xsl:with-param name="type" select="'file'"/>
    </xsl:call-template>
  </xsl:template>

  <xsl:template name="content">
    <xsl:param name="type"/>
    <table>
      <thead>
        <tr>
          <th>Name</th>
          <th>Lines</th>
          <th>Branches</th>
          <th>Conditions</th>
          <th>Calls</th>
        </tr>
      </thead>
      <tbody>
        <xsl:for-each select="entry">
          <xsl:call-template name="entry">
            <xsl:with-param name="type" select="$type"/>
            <xsl:with-param name="class">
              <xsl:choose>
                <xsl:when test="position() mod 2">
                  <xsl:text>odd</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>even</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:with-param>
          </xsl:call-template>
        </xsl:for-each>
      </tbody>
      <tfoot>
        <xsl:for-each select="summary">
          <xsl:call-template name="entry">
            <xsl:with-param name="type" select="'summary'"/>
            <xsl:with-param name="class">
              <xsl:choose>
                <xsl:when test="position() mod 2">
                  <xsl:text>odd</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                  <xsl:text>even</xsl:text>
                </xsl:otherwise>
              </xsl:choose>
            </xsl:with-param>
          </xsl:call-template>
        </xsl:for-each>
      </tfoot>
    </table>
  </xsl:template>
  
  <xsl:template name="entry">
    <xsl:param name="type"/>
    <xsl:param name="class"/>
    <tr class="{$class}">
      <xsl:choose>
        <xsl:when test="$type = 'function'">
          <td class="label"><a href="{@details}.html#{@name}"><xsl:value-of select="@name"/></a></td>
        </xsl:when>
        <xsl:when test="$type = 'file'">
          <td class="label"><a href="{@details}.html"><xsl:value-of select="@name"/></a></td>
        </xsl:when>
        <xsl:otherwise>
          <td class="label">Summary</td>
        </xsl:otherwise>
      </xsl:choose>

      <xsl:if test="count(lines)">
        <xsl:apply-templates select="lines"/>
      </xsl:if>
      <xsl:if test="not(count(lines))">
        <xsl:call-template name="missing"/>
      </xsl:if>

      <xsl:if test="count(branches)">
        <xsl:apply-templates select="branches"/>
      </xsl:if>
      <xsl:if test="not(count(branches))">
        <xsl:call-template name="missing"/>
      </xsl:if>

      <xsl:if test="count(conditions)">
        <xsl:apply-templates select="conditions"/>
      </xsl:if>
      <xsl:if test="not(count(conditions))">
        <xsl:call-template name="missing"/>
      </xsl:if>

      <xsl:if test="count(calls)">
        <xsl:apply-templates select="calls"/>
      </xsl:if>
      <xsl:if test="not(count(calls))">
        <xsl:call-template name="missing"/>
      </xsl:if>

    </tr>
  </xsl:template>
  
  <xsl:template match="lines">
    <xsl:call-template name="row"/>
  </xsl:template>

  <xsl:template match="branches">
    <xsl:call-template name="row"/>
  </xsl:template>

  <xsl:template match="conditions">
    <xsl:call-template name="row"/>
  </xsl:template>

  <xsl:template match="calls">
    <xsl:call-template name="row"/>
  </xsl:template>

  <xsl:template name="missing">
    <td></td>
  </xsl:template>

  <xsl:template name="row">
    <xsl:variable name="quality">
      <xsl:choose>
        <xsl:when test="@coverage = 100">
          <xsl:text>perfect</xsl:text>
        </xsl:when>
        <xsl:when test="@coverage >= 80.0">
          <xsl:text>excellant</xsl:text>
        </xsl:when>
        <xsl:when test="@coverage >= 60.0">
          <xsl:text>good</xsl:text>
        </xsl:when>
        <xsl:when test="@coverage >= 40.0">
          <xsl:text>poor</xsl:text>
        </xsl:when>
        <xsl:when test="@coverage >= 20.0">
          <xsl:text>bad</xsl:text>
        </xsl:when>
        <xsl:otherwise>
          <xsl:text>terrible</xsl:text>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    
    <td class="{$quality}"><xsl:value-of select="@coverage"/>% of <xsl:value-of select="@count"/></td>
  </xsl:template>

</xsl:stylesheet>
